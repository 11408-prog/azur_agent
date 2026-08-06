# Azur Agent 项目说明

> 基于 Qt6 + ElaWidgetTools 构建的 AI 软件开发助手。
> 从"AI 聊天助手"向"AI 软件工程代理"演进中。

**建议顺序：先读本文件的「一、二、三」搞清楚整体架构，
遇到具体问题直接跳到「九、遇到问题该改哪个文件」定位，再进对应源文件。**

---

## 一、项目概览

### 目录结构

```
agent_/
├── CMakeLists.txt                  # 构建配置
│
├── core/                           # AI 引擎层
│   ├── ai_client.h/.cpp            # AI API 客户端（流式 + Function Calling）
│   ├── agent_engine.h/.cpp         # Agent 循环引擎（Chat/Project 共用同一实例）
│   ├── tool_executor.h/.cpp        # 文件操作 + 命令执行工具执行器
│   ├── git_executor.h/.cpp         # Git 集成工具（status/diff/log/commit/branch）
│   ├── contextpolicy.h/.cpp        # ★ 上下文裁剪策略（消除重复的 trim 逻辑）
│   └── promptloader.h/.cpp         # System Prompt 构建器
│
├── data/                           # 数据持久层
│   ├── conversationmanager.h/.cpp  # 对话管理（创建/保存/加载/删除）
│   ├── projectconversationservice.h/.cpp # 项目对话服务层
│   └── appsettings.h/.cpp          # ★ 全项目唯一的 QSettings 读写入口
│
├── ui/                             # 通用 UI 组件
│   ├── messagebubblewidget.h/.cpp  # 消息气泡组件（用户/AI 通用）
│   ├── conversationview.h/.cpp     # ★ 对话展示共享组件（消息列表+输入框+流式节流）
│   ├── markdownrenderer.h/.cpp     # Markdown → HTML 渲染器
│   └── uiconstants.h               # ★ 跨文件共用的 UI 常量（spinner 动画帧）
│
├── chat/                           # 聊天模式
│   └── chatpagewidget.h/.cpp       # 聊天模式页面
│
├── project/                        # 项目模式
│   ├── projectpage.h/.cpp          # 项目模式页面：三栏布局 + 发送逻辑
│   ├── project_analyzer.h/.cpp     # 项目索引分析器
│   ├── projectsession.h/.cpp       # 项目会话持久化（.azur/project.json）
│   ├── activitypanel.h/.cpp        # AI 活动步骤展示面板
│   ├── projecthistorydialog.h/.cpp # 项目历史记录对话框
│   ├── confirmdialogs.h/.cpp       # ★ 写操作确认弹窗（Chat/Project 共用）
│   └── projectconvdialog.h/.cpp    # 项目对话列表对话框
│
├── app/                            # 应用主框架
│   ├── main.cpp                    # 入口：日志钩子 + ElaApplication 初始化
│   ├── mainwindow.h/.cpp           # UI 主框架：导航、AppBar、Chat 模式的发送逻辑
│   ├── modemanager.h/.cpp          # Chat/Project 双模式切换与项目状态管理
│   ├── settingpagewidget.h/.cpp    # 设置页面（UI 控件，实际存储都走 AppSettings）
│   ├── app.qrc                     # Qt 资源文件
│   ├── resources/                  # Prompt 资源文件
│   │   ├── full/                   # 完整版人格（云端模型推荐）：agent_core + 人格 + 台词三件套
│   │   ├── lite/                   # 精简版人格（本地小模型推荐）
│   │   ├── enterprise.md           # 旧版单文件人格（当前不再加载，待清理）
│   │   └── 碧蓝航线 企业.md         # 角色背景设定
│   └── avatar/                     # 头像图片
│
├── lib/ElaWidgetTools/             # 预编译的 ElaWidgetTools 库
├── build/                          # 构建输出
├── daily_log/                      # 运行日志
└── .azur/                          # 项目缓存目录
    ├── project.json                # 项目会话配置
    └── project_index.json          # 项目索引缓存 ★ 自动生成
```

标 ★ 的文件（`confirmdialogs` / `uiconstants` / `appsettings` / `contextpolicy` / `conversationview`）是为了消除重复代码新增的公共模块，**不属于独立业务逻辑，只被别的模块引用**，见「三、6」和「三、7」。

### 技术栈

| 项目 | 内容 |
|------|------|
| 语言 | C++17 |
| 框架 | Qt 6.5+ (Core / Widgets / Network) |
| UI | ElaWidgetTools（预编译本地库） |
| 构建 | CMake 3.19+ |
| AI API | OpenAI Chat Completions 兼容（流式 + Function Calling） |

---

## 二、架构总览

```
┌───────────────────────────────────────────────────────────────┐
│  MainWindow (ElaWindow)                                        │
│  ┌────────────────────────────────────────────────────────┐    │
│  │  Navigation: 对话 | 项目 | 设置 | 关于                   │    │
│  │  AppBar: [打开文件夹] [对话列表] [历史记录] [侧栏]         │    │
│  └────────┬──────────────────────────┬──────────────────────┘    │
│           │                          │                          │
│  ┌────────▼────────┐    ┌───────────▼────────────┐             │
│  │  Chat Mode      │    │  Project Mode           │             │
│  │  ChatPageWidget │    │  ProjectPage            │             │
│  │  ┌───────────┐  │    │  ┌────┬──────┬──────┐  │             │
│  │  │侧栏│ 对话   │  │    │  │文件│ Agent│ 活动 │  │             │
│  │  │历史│ 区域   │  │    │  │树  │ 对话 │ 面板 │  │             │
│  │  └───────────┘  │    │  └────┴──────┴──────┘  │             │
│  └────────┬────────┘    └───────────┬────────────┘             │
│           │                          │                          │
│           └──────────┬───────────────┘                          │
│                      │  两个模式的发送逻辑都会先检查              │
│              ┌───────▼────────┐  engine->isBusy()，避免互相打断  │
│              │  AgentEngine   │  （见「五、1」）                  │
│              │  (AI 请求循环)  │                                 │
│              └──┬─────────┬───┘                                 │
│                 │         │                                     │
│        ┌────────▼──┐  ┌──▼──────────┐                          │
│        │ AI Client │  │ ToolExecutor│                          │
│        │ (流式API)  │  │ (文件+命令) │                          │
│        └───────────┘  └─────────────┘                          │
│                                                                 │
│  ┌─────────────┐ ┌──────────────────┐ ┌──────────────────┐    │
│  │ ModeManager │ │ConversationManager│ │  ProjectAnalyzer │    │
│  │ (模式切换)   │ │  (对话持久化)      │ │  (项目索引)       │    │
│  └─────────────┘ └──────────────────┘ └──────────────────┘    │
│                                                                 │
│  ┌────────────┐ ┌────────────┐ ┌──────────────┐ ┌───────────┐ │
│  │PromptLoader│ │Markdown    │ │ConfirmDialogs│ │AppSettings│ │
│  │(Prompt构建) │ │Renderer    │ │(确认弹窗共用)│ │(设置读写)  │ │
│  └────────────┘ └────────────┘ └──────────────┘ └───────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

---

## 三、文件详解

### 3.1 入口层

#### `app/main.cpp` (70 行)

程序入口。职责：

1. **日志钩子** — 将所有 `qDebug()`/`qWarning()` 输出重定向到 `daily_log/azur_debug_YYYY-MM-DD.log`
2. **ElaApplication 初始化** — ElaWidgetTools 库的要求
3. **创建并显示 MainWindow**

---

### 3.2 UI 主框架 + 模式切换

#### `mainwindow.h/.cpp` (约 730 行)

核心 UI 框架，继承 `ElaWindow`。负责导航栏/AppBar 搭建、Chat 模式的发送逻辑，双模式切换的业务逻辑已经委托给 `ModeManager`。

**导航管理：**
- 4 个页面节点：对话、项目、设置、关于
- 通过 `addPageNode()` / `addFooterNode()` 构建侧边导航
- 通过 `eventFilter` 监听 ProjectPage 的显示/隐藏，通知 `ModeManager` 切换模式

**Chat 模式发送流程（`onSendClicked`）会依次检查：**
1. 模型名称是否已填写
2. `chatEngine_->isBusy()` — 如果 Project 模式正占用共享的引擎，直接提示"稍后再发"，不会抢占对方的请求（这是这一轮排查过的一个真实 bug 修复，见「五、1」）
3. 从 `AppSettings::agentPermission()` 同步"自动执行"开关到引擎
4. 追加消息 → `chatEngine_->start(...)`

**AppBar 右侧按钮区：**
- `sidebarToggleBtn_` — 折叠左侧面板
- `openFolderBtn_` — 打开项目文件夹（仅 Project 模式可见）
- `projectConvListBtn_` / `projectHistoryBtn_` — 项目对话列表 / 历史记录对话框

**关键依赖：**
```
MainWindow
├── ChatPageWidget         (聊天页面)
├── ProjectPage            (项目页面)
├── SettingPageWidget      (设置页面)
├── ModeManager            (双模式切换逻辑)
├── AgentEngine            (AI 引擎，Chat/Project 共用同一实例)
├── ConversationManager    (对话持久化，Chat 用)
└── DeepSeekClient         (API 客户端)
```

#### `modemanager.h/.cpp` (约 210 行)

Chat / Project 双模式切换的状态机，从 `MainWindow` 解耦而来。**这是理解"点导航栏切到项目模式后发生了什么"的入口文件。**

**核心职责：**
- `enterProjectMode()` / `enterChatMode()` — 模式切换，首次进入项目模式会弹出目录选择框
- `finishProjectInit()` — 延迟执行的重初始化：加载历史对话、加载 system prompt、重建项目索引
- `switchToEntry()` / `switchToConversation()` / `openProject()` — 项目/对话之间的切换，切换前都会先 `saveConversation()` 落盘当前对话
- 持有 `currentProject_`（`ProjectSession*`）——当前项目的路径和路径白名单

---

### 3.3 聊天模式

#### `chatpagewidget.h/.cpp` (约 590 行)

聊天页面的独立组件。

**布局：**
```
┌──────────────┬──────────────────────────────┐
│  侧栏(历史)   │         消息区域               │
│  QListWidget  │  ElaScrollArea              │
│              │   ┌──────────────────────┐   │
│              │   │  MessageBubbleWidget  │   │
│              │   │  ...                  │   │
│              │   └──────────────────────┘   │
│              │  ┌──────────────────────┐    │
│              │  │ 输入框 + 发送按钮     │    │
│              │  └──────────────────────┘    │
└──────────────┴──────────────────────────────┘
```

**接收的信号（来自 AgentEngine）：**
- `chunkReceived` → `onChunkReceived()` → 流式追加到当前气泡
- `finished` → `onResponseCompleted()` → 完成渲染
- `stepChanged` → `updateAiStep()` → 步骤指示器（spinner 帧取自 `UiConstants::kSpinnerFrames`）

> 之前这个文件里有一段 145 行、用 `#if 0` 包住的旧版 `appendMessage_OLD`，已经删除；如果你在 git log 里翻到过这个函数名，那是历史遗留，现在的实现只有 `appendMessage()` 一份。

---

### 3.4 项目模式

#### `projectpage.h/.cpp` (约 680 行)

项目开发模式的三栏布局页面，同时也是 Project 模式下的发送逻辑所在地。

**布局：**
```
┌──────────────┬────────────────────────┬──────────────┐
│  左侧面板     │      中间面板           │  右侧面板     │
│  (220px)     │     Agent 对话          │  (250px)     │
│  项目文件树   │  MessageBubbleWidget   │  AI 活动     │
│  QTreeView   │  ...                   │  面板        │
│  +QFileSystem│  ┌────────────────┐    │  Activity-   │
│  Model       │  │ 输入框 + 发送   │    │  Panel       │
│              │  └────────────────┘    │              │
└──────────────┴────────────────────────┴──────────────┘
```

**发送流程（`onSendClicked`）会依次检查：**
1. 从 `AppSettings` 读取 `apiKey` / `baseUrl` / `model`
2. `engine_->isBusy()` — 如果 Chat 模式正占用共享引擎，提示"稍后再发"
3. `engine_->setAutoExecute(AppSettings::agentPermission() == 1)` 同步权限设置
4. `engine_->start(...)`，之后 `setInputEnabled(false)` 禁用输入框直到收到回复
   （这行之前是缺失的，之前可以在 AI 生成过程中反复点发送，已修复，见「五、1」）

**关键特性：**
- 左右面板可折叠（带动画，折叠状态持久化在 `AppSettings::projectLeftPanelCollapsed()` 等）
- 项目路径变更时自动重建索引（`rebuildIndex()`）
- `restoreConversation()` 切换项目/对话时会连带 `activityPanel_->clear()`，避免不同项目的活动记录混在一起

**与 ChatPageWidget 核心区别：**
- 没有历史侧栏，换成文件树 + 活动面板
- 使用 `ToolExecutor::toolDefinitions()` 注册文件/命令工具
- system prompt 末尾附带项目索引摘要

---

### 3.5 Agent 引擎与工具执行

#### `agent_engine.h/.cpp` (约 310 行)

Agent 循环核心，封装了"请求 AI → 解析工具调用 → 执行工具 → 再次请求"的完整循环。**Chat 模式和 Project 模式共用同一个实例**（见「五、1」的风险说明）。

**核心流程：**
```
start() ──→ sendRequest() ──→ AI 返回流式文本
                                    │
                                    ├── 纯文本 → onResponseCompleted() → doFinish()
                                    │
                                    └── tool_calls → onToolCallsReceived()
                                          │
                                   ┌──────┴──────┐
                                   │              │
                    有写操作 且 !autoExecute_    无写操作 或 autoExecute_==true
                              │                  │
                        previewDiff()      executeToolCalls()
                              │                  │
                     emit writeConfirmation  ┌────┘
                              │              │
                     ┌────用户确认────┐     │
                     │ 接受  │  拒绝  │     │
                     │       │       │     │
                execute   doFail()   │     │
                   │                 │     │
                   └────────┬────────┘     │
                            │              │
                      toolRound_++         │
                            │              │
                      sendRequest() ◄──────┘
```

**关键成员：**
- `messageHistory_` — 完整消息历史（含 tool 消息）
- `toolRound_` / `maxToolRounds_` — 防止无限循环（默认上限 200 轮）
- `waitingConfirm_` — 写操作等待用户确认状态
- `autoExecute_` — 对应设置页"Agent 权限"开关，`setAutoExecute()` 由调用方在 `start()` 前设置，true 时跳过确认弹窗
- `isBusy()` — `isRunning_ || waitingConfirm_`，Chat/Project 两边发消息前都会先查这个，防止共享引擎被互相抢占

**信号：**
| 信号 | 触发时机 |
|------|----------|
| `chunkReceived(delta)` | 流式文本片段 |
| `stepChanged(text)` | 步骤状态变化 |
| `finished(fullText)` | 循环正常结束 |
| `errorOccurred(msg)` | 错误终止 |
| `writeConfirmationRequired(diffList)` | 需要用户确认写操作（`autoExecute_==false` 时才会触发） |

#### `ai_client.h/.cpp` (约 320 行)

AI API 客户端，负责与 OpenAI/DeepSeek 兼容接口通信。

**SSE 流式处理关键设计：**
- `sseLineBuffer_` — 持久化缓冲区，处理网络分包导致的不完整行（一条 `data: {...}` 可能被拆成两次 `readyRead()`）
- `PendingToolCall` — 按 `index` 累积拼接工具调用分片（`arguments` 是分段 JSON 字符串）
- `sawToolCallFinish_` — 检测 `finish_reason: "tool_calls"`
- **兜底逻辑** — 非流式响应也兼容处理

#### `tool_executor.h/.cpp` (约 1050 行)

文件操作与命令执行器，**所有写操作和命令执行都需要经过用户确认（除非设置了"自动执行"）**。

**十个工具：**
| 工具 | 功能 | 是否需要确认 | 安全限制 |
|------|------|------|----------|
| `read_file` | 读取文本文件内容 | 否 | ≤300KB，跳过二进制，路径限制在工作区内 |
| `list_directory` | 列出目录内容 | 否 | ≤200 条 |
| `write_file` | 创建/覆盖写入文件 | **是** | ≤1MB，自动创建父目录，路径限制在工作区内 |
| `apply_patch` | 搜索-替换局部修改 | **是** | ≤20 个 patch/次，路径限制在工作区内 |
| `run_command` | 在项目目录下执行终端命令 | **是** | 30s 超时，高危命令黑名单，**不受工作区路径限制**（命令内部可以访问任意路径，务必仔细看确认弹窗里的命令内容） |
| `git_status` | 显示仓库状态（相当于 git status） | 否 | 仅读取，不修改 |
| `git_diff` | 查看代码变更内容（相当于 git diff） | 否 | 返回按文件分组的 diff，最多 200 行 |
| `git_log` | 查看提交历史（相当于 git log --oneline） | 否 | 可配置显示条数，最多 100 条 |
| `git_commit` | 暂存所有变更并创建提交（git add -A && git commit） | **是** | 自动 `git add -A`，提交信息由 AI 生成 |
| `git_branch` | 列出本地分支（相当于 git branch） | 否 | 仅读取，不创建或切换分支 |

**安全设计：**
- `resolveSafePath()` — 路径穿越防护，`read_file`/`write_file`/`apply_patch` 的目标路径必须在工作区目录内；**`run_command` 不走这个检查**，因为命令字符串本身无法做路径级别的限制，只能靠黑名单拦高危操作
- `isBlacklistedCommand()` — 对 `rm`/`rmdir` 逐 token 拆解短选项簇检测（能识别 `-rf`、`-Rf`、`-irf` 等各种组合写法，不是简单的固定子串匹配）
- `isWriteTool()` — 判断一个工具是否需要走确认流程，目前是 `write_file` / `apply_patch` / `run_command` / `git_commit` 四个

**apply_patch 的匹配逻辑（`applyPatchesToContent()`）：**
- 精确匹配失败后自动尝试 `findFuzzyLineMatch()`（忽略行首尾空白）
- 匹配不唯一时报错，防止误改
- 逐 patch 应用，中途失败立即整体失败，不做部分应用
- **这个函数同时被 `applyPatch()`（真正执行）和 `previewDiff()`（确认弹窗预览）调用**——以前这两处各写了一份，逻辑容易跑偏导致"预览看到的"和"实际发生的"不一致，现在统一成一份，改的时候两边永远同步（见「五、2」）

**Diff 生成：**
- 基于 LCS 动态规划，正确计算插入/删除/保留行
- 超过 400 万 dp cell 阈值时跳过（防卡顿），给摘要提示

#### `contextpolicy.h/.cpp` (约 420 行)

轻量级上下文裁剪策略类。**纯策略，不维护任何状态**，输入 `AgentEngine` 的消息历史，输出裁剪后的副本。

**核心函数：**
- `trim(messages, maxMessages, maxToolResults, maxTotalSize, protectedTurns, report)` — 静态函数，执行多阶段裁剪

**裁剪策略：**
1. **system 消息永久保护**，永不丢弃
2. **最近的 N 轮对话**（user/assistant）受年龄保护（`protectedTurns` 参数控制，默认 15 轮）
3. **tool 消息分级**：写操作/错误结果 = 高价值，命令执行 = 中价值，读操作 = 低价值（优先丢弃）
4. **tool call 完整性保护**：如果 assistant（含 tool_calls）被保留，它所有的 tool 结果也必须保留，否则 API 会拒绝请求

**诊断报告：**
`ContextReport` 结构体记录裁剪前后的数量/大小/分类统计，供日志和 UI 展示。

这个类目前被 `AgentEngine` 在每次 `sendRequest()` 时调用，防止 token 预算爆炸。属于 V3.5（上下文管理）的早期铺垫。

---

### 3.6 共用工具模块 ★

这些文件不承载独立业务逻辑，是这一轮为了消除重复代码单独抽出来的，**改跨模块共用的东西，先看这里有没有现成的，别急着复制粘贴**。

#### `confirmdialogs.h/.cpp` (约 60 行)

`ConfirmDialogs::confirmWriteOperations(parent, diffList)` — 展示写操作/命令执行的 diff 预览弹窗，返回用户是否接受。`mainwindow.cpp`（Chat 模式）和 `projectpage.cpp`（Project 模式）都调用它，不再各写一份。**要改确认弹窗的样式、文案、交互（比如加个"总是允许"按钮），改这一个文件就行，两个模式会同时生效。**

#### `ui/uiconstants.h`

目前只有一个常量：`UiConstants::kSpinnerFrames`，Agent"思考中/执行中"用的旋转动画帧（盲文字符）。`activitypanel.cpp` / `chatpagewidget.cpp` / `messagebubblewidget.cpp` 都引用它。C++17 inline 变量，直接在头文件里定义，多个 .cpp 包含不会报重复定义。

#### `appsettings.h/.cpp` (约 55 行)

**全项目唯一允许出现 `QSettings("AzurStudio", "AzurAgent")` 构造调用的文件**（在 `appsettings.cpp` 内部）。所有配置项的读写都通过 `AppSettings::xxx()` 静态函数完成，一眼能看到项目一共有哪些配置：

| 函数 | 对应设置 | 默认值 |
|------|----------|--------|
| `chatApiKey()` / `setChatApiKey()` | 聊天模式 API Key | 空 |
| `chatBaseUrl()` / `setChatBaseUrl()` | 聊天模式 Base URL | `https://api.deepseek.com` |
| `chatModel()` / `setChatModel()` | 聊天模式模型名称 | `deepseek-v4-flash` |
| `projectApiKey()` / `setProjectApiKey()` | 项目模式 API Key | 空 |
| `projectBaseUrl()` / `setProjectBaseUrl()` | 项目模式 Base URL | `https://api.deepseek.com` |
| `projectModel()` / `setProjectModel()` | 项目模式模型名称 | `deepseek-v4-flash` |
| `recentModels()` / `setRecentModels()` | 最近使用的模型列表 | 空 |
| `agentPermission()` / `setAgentPermission()` | Agent 权限：0=每次确认，1=自动执行 | 0 |
| `startupMode()` / `setStartupMode()` | 默认模式：0=聊天，1=项目 | 0（**目前只存不读，见「七、待办」**） |
| `chatPromptMode()` / `setChatPromptMode()` | 聊天人格模式：0=精简（本地模型推荐），1=完整（云端模型推荐） | 0 |
| `bgOpacity()` / `setBgOpacity()` | 聊天背景透明度 | 25 |
| `chatBgEnabled()` / `setChatBgEnabled()` | 显示聊天背景（开关，默认关，未启用时透明度无效） | false |
| `showStatusBar()` / `setShowStatusBar()` | 显示底部状态栏 | `true` |
| `projectLeftPanelCollapsed()` / `Right...()` | 项目模式左右面板折叠状态 | false |
| `lastProjectPath()` / `setLastProjectPath()` | 上次打开的项目路径 | 空 |
| `projectHistory()` / `setProjectHistory()` | 最近项目列表（JSON 数组，最多10条） | 空数组 |
| `projectConvMigrationDone()` / `set...()` | 旧版对话迁移是否已完成 | false |

**新增一个配置项**：在 `appsettings.h` 加一对声明、在 `appsettings.cpp` 加一对实现，不要在业务代码里直接 `new QSettings(...)`。

---

### 3.7 其它 UI 组件

#### `conversationview.h/.cpp` (约 400 行)

聊天消息展示区域的**共享组件**：消息气泡列表 + 输入框 + 发送/停止按钮 + 流式内容节流刷新。

被 `ChatPageWidget` 和 `ProjectPage` 共同持有，取代了原来两边各写一份的 `appendMessage` / `clearChatDisplay` / `onChunkReceived` / `flushAiContent` / `setInputEnabled` 等逻辑（约 150 行结构性重复代码）。

**设计原则：** 只负责「展示」，不知道 `AgentEngine`，不知道自己是 Chat 模式还是 Project 模式。发送按钮/回车键只会发出 `sendRequested` 信号，是否调用引擎、要不要做跨模式检查等业务逻辑由外层决定。

**关键特性：**
- 流式输出 50ms 节流定时器，避免逐 token 触发重排版
- 支持设置自定义按钮尺寸和输入框高度（Chat/Project 布局不同）
- 自适应滚动到底部
- `flushPendingContent()` — 停止节流并立即刷新，确保回复/错误时不会丢最后一小段
- 底部状态栏：显示当前模型名与实时时间，可通过设置开关显隐
- 发出 `viewportResized` / `firstChunkOfResponse` 信号供外层（如 Chat 模式）做背景适配和步骤指示器更新

> 页面特有的东西不在这里：Chat 模式的步骤指示器（气泡内 spinner）由 ChatPageWidget 自行叠加；项目模式的外部 ActivityPanel 也独立管理。

#### `messagebubblewidget.h/.cpp` (约 350 行)

消息气泡组件，同时支持用户消息和 AI 消息。AI 消息走 Markdown 渲染（代码块带"复制"按钮 + 红绿灯装饰），支持流式内容实时更新。

#### `activitypanel.h/.cpp` (约 215 行)

AI 活动步骤展示面板，实时显示 Agent 当前执行状态（Pending 转圈 / Completed ✓ / Failed ✗）。切换项目/对话时会被 `ProjectPage::restoreConversation()` 调用 `clear()` 清空。

#### `markdownrenderer.h/.cpp`

轻量级 Markdown → HTML 转换器：代码块、行内代码、加粗/斜体、链接、Diff 语法高亮。

- 代码块采用浅色背景（`#f6f8fa`）+ 灰色头部，类似 GitHub 风格
- 行内代码用浅灰底深灰字
- Diff 语法高亮：`+` 行绿色、`-` 行红色

#### `settingpagewidget.h/.cpp`

设置页面，采用卡片式布局（`QFrame` 圆角卡片），分为四个分组：
- **聊天模式连接**：API Key / Base URL / 模型名称（聊天专用）
- **项目模式连接**：API Key / Base URL / 模型名称（项目专用）
- **Agent 行为**：默认模式 / Agent 权限 / 聊天人格模式（精简 / 完整）
- **界面与外观**：显示聊天背景开关（默认关）/ 聊天背景透明度 / 底部状态栏显隐开关

**本身不直接持有配置状态**，所有读写都转发给 `AppSettings`。

#### `projecthistorydialog.h/.cpp` / `projectconvdialog.h/.cpp`

项目历史记录对话框 / 当前项目对话列表对话框，数据来源分别是 `AppSettings::projectHistory()` 和 `ConversationManager`。

---

### 3.8 项目索引

#### `project_analyzer.h/.cpp` (约 710 行)

离线项目结构分析器，让 AI 在开始工作前就知道项目全貌。索引缓存在 `.azur/project_index.json`。

**`needsRebuild()` 目前的已知局限**：只会对比"上次索引里已经收录的文件"是否被修改/删除，**新增的源文件不会触发重建**，直到用户改动了某个旧文件才会连带把新文件也扫进去。如果发现 AI 对新建的文件"视而不见"，先看这里（见「七、待办」）。

---

### 3.9 数据管理层

#### `conversationmanager.h/.cpp` (约 245 行)

对话持久化管理器，存储在 `{AppData}/AzurAgent/data/`（`conversations.json` 元信息 + `chats/*.json` 消息内容）。

#### `projectsession.h/.cpp` (65 行)

项目会话数据，保存在项目目录下的 `.azur/project.json`（路径 + 路径白名单）。

#### `projectconversationservice.h/.cpp` (约 245 行)

项目对话管理服务层，封装保存/加载/切换/迁移逻辑。历史记录相关的读写都走 `AppSettings::projectHistory()`。

#### `promptloader.h/.cpp` (36 行)

- `buildSystemPrompt()` — 项目模式：拼接 `resources/full/` 下的 `agent_core.md` + `enterprise_personality.md` + `enterprise_quotes.md` 成完整 system prompt。
- `buildChatSystemPrompt(style)` — 聊天模式：style=1 完整版（同项目模式三件套），style=0 精简版（`resources/lite/enterprise.md`），由设置项 `chatPromptMode` 控制；聊天不需要工具调用，精简版不加载 agent_core。

---

## 四、数据流

### 4.1 Chat 模式

```
用户输入
  ▼
MainWindow::onSendClicked()
  │ 模型名称检查 → chatEngine_->isBusy() 检查（防止抢占 Project 请求）
  │ 从 AppSettings 同步 agentPermission → chatEngine_->setAutoExecute()
  │ messageHistory_.append(userMsg)
  ▼
AgentEngine::start(apiKey, baseUrl, model, messageHistory, systemPrompt, tools=[], workspaceRoot="")
  │
  ├── sendRequest() → DeepSeekClient::sendMessage() → AI 流式返回
  │     ├── chunkReceived(delta) → onApiChunkReceived() → ChatPageWidget::onChunkReceived()
  │     └── responseCompleted(fullText) → onApiResponseCompleted() → ConversationManager::saveConversation()
  │
  └── tools 为空，Chat 模式实际上不会触发 writeConfirmationRequired 这条路径
```

### 4.2 Project 模式

```
打开项目
  ▼
ModeManager::openProject() / switchToEntry() / finishProjectInit()
  ├── ProjectPage::setProjectPath(path) → 文件树刷新 + rebuildIndex()
  └── ProjectPage::restoreConversation(messages) → 清空显示 + activityPanel_->clear()

用户发送消息
  ▼
ProjectPage::onSendClicked()
  │ 从 AppSettings 读取 apiKey/baseUrl/model
  │ engine_->isBusy() 检查（防止抢占 Chat 请求）
  │ engine_->setAutoExecute(AppSettings::agentPermission() == 1)
  ▼
engine_->start(..., systemPrompt_ + indexSummary_, ToolExecutor::toolDefinitions(), projectPath_)
  │ setInputEnabled(false)
  ▼
AgentEngine 循环:
  ├── 纯文本 → finished → onResponseCompleted()
  └── tool_calls → onToolCallsReceived()
        ├── 有写操作 且 未开启自动执行
        │     → previewDiff() → writeConfirmationRequired()
        │     → ConfirmDialogs::confirmWriteOperations() 弹窗
        │     → 接受 confirmWrite(true) → executeToolCalls()
        │     → 拒绝 confirmWrite(false) → doFail()
        └── 无写操作，或已开启自动执行 → executeToolCalls() → sendRequest() 继续下一轮
```

---

## 五、关键设计决策（含历史问题修复记录）

### 1. 双模式共用一个 AgentEngine 实例

Chat 模式和 Project 模式共享同一个 `AgentEngine` 实例，传入不同的 `tools`/`workspaceRoot`/`systemPrompt`。好处是不用维护两套引擎；**代价是如果不加防护，一方发起新请求会通过 `start()` 内部的 `cancel()` 悄悄打断另一方正在跑的请求，且不发出任何通知**。

现在两边的 `onSendClicked()` 在真正调用 `engine->start()` 之前都会先检查 `!isWaitingResponse_ && engine->isBusy()`，如果引擎被对方占用就提示用户稍后再试，不会再静默抢占。**如果你发现"明明没在等回复，输入框却一直被禁用"，大概率是这个跨模式占用逻辑判断出了问题，去 `mainwindow.cpp`/`projectpage.cpp` 的 `onSendClicked()` 和 `agent_engine.h` 的 `isBusy()` 排查。**

### 2. 写操作确认机制，预览与执行必须用同一套逻辑

写操作（含 `run_command`）不会立即执行：`previewDiff()` 生成预览 → `ConfirmDialogs` 弹窗展示 → 用户选择接受/拒绝。

`apply_patch` 的预览和真正执行以前是两份独立实现，容易跑偏（预览显示"没变化"但实际执行却生效，或反过来）。现在两处都调用同一个 `ToolExecutor::applyPatchesToContent()`，**改 patch 匹配逻辑只需要改这一个函数**，不用记得两头一起改。

### 3. Agent 权限：每次确认 / 自动执行

设置页的"Agent 权限"下拉框现在是真正生效的（`AppSettings::agentPermission()`，0=每次确认，1=自动执行）。选"自动执行"后 `AgentEngine::autoExecute_` 为 true，写操作/命令会跳过确认弹窗直接执行，但仍会在活动面板留一条"⚙ 自动执行"的记录，不是完全静默。

### 4. run_command 也需要确认

`run_command` 之前是唯一一个不需要用户确认就会直接执行的工具，风险其实比文件写入更高（不受工作区路径限制）。现在它和 `write_file`/`apply_patch` 一样纳入确认流程，预览弹窗里会显示即将执行的完整命令。

### 5. 项目索引的缓存策略

首次打开项目全量扫描并缓存到 `.azur/project_index.json`；再次打开对比每个已收录文件的 `lastModified`。**已知局限：新增文件不会触发重建**，见「三、8」。

### 6. ConversationView — 消除 Chat/Project 的消息展示重复代码

`ChatPageWidget` 和 `ProjectPage` 原来各自维护一份几乎一样的 `appendMessage()` / `onChunkReceived()` / 流式节流 / 输入框控制逻辑（约 150 行重复代码）。

现在抽出了 `ConversationView` 共享组件，两边各自持有实例，只保留各自特有的逻辑（Chat 模式的气泡内步骤指示器、Project 模式的与 ActivityPanel 联动）。设计原则是「只负责展示，不接触业务逻辑」，发送按钮只管发信号，要不要真的调用 `AgentEngine` 由外层决定。

---

## 六、构建与运行

### 依赖

- Qt 6.5+（Core, Widgets, Network）
- ElaWidgetTools（预编译，位于 `lib/ElaWidgetTools/`）
- CMake 3.19+
- MinGW 或其他 C++17 编译器

### 构建步骤

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build .
```

### 调试日志

所有 `qDebug()` 输出重定向到：
```
agent_/daily_log/azur_debug_YYYY-MM-DD.log
```

---

## 七、开发路线 & 已知待办

| 阶段 | 目标 | 完成度 |
|------|------|--------|
| V1.5 | 文件操作（write_file / apply_patch / diff / 确认） | ✅ 已完成 |
| V2.0 | Agent Engine 重构 + 工具循环 | ✅ 基本完成 |
| V2.1 | 项目索引（ProjectAnalyzer） | ✅ 已完成 |
| V2.5 | 命令执行（run_command） | ✅ 已完成 |
| V2.6 | 安全加固：run_command 确认 / Agent权限接入 / 共享引擎抢占防护 / 重复代码消除 | ✅ **本次完成** |
| V3.0 | Git 集成 | ✅ **已完成** |
| V3.5 | Git 增强（create/checkout/merge）+ 代码解析（Tree-sitter） | ❌ 未开始 |
| V3.5 | 上下文管理（ContextPolicy + ContextManager） | 🚧 **进行中**（ContextPolicy 已完成） |
| V4.0 | 长期记忆 + 多 Agent 协作 | ❌ 未开始 |

**已知待办（不是 bug，是明确还没做的事）：**
1. `AppSettings::startupMode()` 目前只存不读——设置页选"启动后进入项目模式"不会真的在启动时生效，因为涉及 `ElaWindow` 的页面导航 API，需要先确认具体用哪个函数切换页面
2. `ProjectAnalyzer::needsRebuild()` 检测不到新增文件（见「三、8」）
3. ~~`ChatPageWidget` 和 `ProjectPage` 之间仍有结构性重复（消息气泡渲染、流式输出节流、滚动到底部等逻辑两边各一份），值得抽一个共享的"对话展示"组件，但这是个大重构，还没做~~
   ✅ **已解决**：`ConversationView` 组件抽离完成，两边共用同一份实现（见「三、7」）

---

## 八、类关系图

```
MainWindow (ElaWindow)
  ├── owns → DeepSeekClient (1)
  ├── owns → AgentEngine (1, 与 ProjectPage 共享)
  ├── owns → ModeManager (1)
  ├── owns → ConversationManager (2: Chat + Project)
  ├── owns → ProjectConversationService (1)
  ├── owns → ChatPageWidget (1)
  │     ├── uses → ConversationView (1, 共用消息展示)
  │     └── uses → MessageBubbleWidget (N) → MarkdownRenderer (静态)
  ├── owns → ProjectPage (1)
  │     ├── uses → AgentEngine (1, 与 MainWindow 共享)
  │     ├── uses → ProjectAnalyzer / ToolExecutor (静态)
  │     ├── owns → ActivityPanel (1)
  │     ├── uses → ConversationView (1, 共用消息展示)
  │     └── uses → MessageBubbleWidget (N)
  ├── owns → SettingPageWidget (1)
  └── uses → ConfirmDialogs / AppSettings (静态/命名空间函数，无实例)

ModeManager
  ├── owns → ProjectSession (1, 当前项目)
  └── uses → ProjectPage / ProjectConversationService

AgentEngine
  ├── owns → DeepSeekClient (1, 外部传入)
  ├── uses → ToolExecutor (静态)
  └── uses → ContextPolicy (静态策略，消息发送前 trim)

任何模块
  └── uses → AppSettings (命名空间函数，内部唯一持有真正的 QSettings)
```

---

## 九、遇到问题该改哪个文件

按"症状"找文件，而不是按"文件名"找症状。

| 症状 / 需求 | 去哪个文件 |
|---|---|
| AI 回复不显示 / 流式输出卡住 | `ai_client.cpp`（SSE 解析）→ `agent_engine.cpp`（`onChunkReceived`）→ `chatpagewidget.cpp` 或 `projectpage.cpp` 的 `onChunkReceived` |
| AI 调用工具没反应 / 工具报错 | `tool_executor.cpp`（对应工具的执行函数）→ 看 `execute()` 里的路由 |
| 写文件/改文件的确认弹窗内容不对，或想加个"总是允许"按钮 | `confirmdialogs.cpp`（唯一实现，Chat/Project 都受影响） |
| 想加一个新工具（比如 `delete_file`） | `tool_executor.h/.cpp`：`toolDefinitions()` 加 schema → 写执行函数 → `execute()` 注册路由 → 如果是写操作，`isWriteTool()` 和 `previewDiff()` 也要加 |
| 想改"自动执行 / 每次确认"的行为 | `agent_engine.cpp` 的 `onToolCallsReceived()`（`autoExecute_` 判断逻辑） |
| 想加一个新的设置项 | `appsettings.h/.cpp` 加一对读写函数，**不要**在别处直接 `new QSettings` |
| 设置页某个选项选了没反应 | 先看 `appsettings.cpp` 里有没有对应的读函数真的被别处调用了；`startupMode` 目前就是"只存不用"的已知情况（见「七」） |
| Chat 模式和 Project 模式互相打断（一边发消息把另一边的回复打断了） | `agent_engine.h` 的 `isBusy()` + `mainwindow.cpp`/`projectpage.cpp` 里 `onSendClicked()` 开头的忙碌检查 |
| 输入框在 AI 回复期间没被禁用，能重复发送 | `projectpage.cpp` 的 `onSendClicked()` 结尾要有 `setInputEnabled(false)`；`mainwindow.cpp` 是靠 `isWaitingResponse_` |
| 取消请求后无法再发送新消息 | `mainwindow.cpp` 里 `cancelRequested` 的 lambda，检查 `isWaitingResponse_` 有没有被重置为 `false` |
| Spinner 动画样式想改 | `ui/uiconstants.h`（`kSpinnerFrames` 常量），改这一处全项目生效 |
| 项目文件树 / 索引没更新 | `project_analyzer.cpp` 的 `needsRebuild()`（已知不检测新增文件）→ `projectpage.cpp` 的 `rebuildIndex()` |
| 活动面板（右侧步骤记录）内容堆积/不清空 | `projectpage.cpp` 的 `restoreConversation()` 有没有调用 `activityPanel_->clear()` |
| 对话历史丢失 / 保存不对 | Chat 走 `conversationmanager.cpp`；Project 走 `projectconversationservice.cpp` + `modemanager.cpp` 的 `saveConversation()`/`saveEntry()` |
| 项目路径/白名单相关的权限问题 | `projectsession.cpp`（`.azur/project.json`）+ `tool_executor.cpp` 的 `resolveSafePath()` / `s_allowedPaths` |
| 命令执行被误拦截 / 危险命令没拦住 | `tool_executor.cpp` 的 `isBlacklistedCommand()` |
| Markdown 渲染样式不对（代码块/表格/加粗等） | `markdownrenderer.cpp` |
| 消息气泡样式/头像不对 | `messagebubblewidget.cpp` |
| 模式切换（Chat ↔ Project）逻辑不对 | `modemanager.cpp` |
| Chat/Project 的消息展示（气泡列表、流式刷新、输入框）行为不一致 | `ui/conversationview.h/.cpp` — 两者共用的消息展示组件，见「三、7」 |
| AI 回复占用 token 过多，或消息历史太长导致 API 报错 | `core/contextpolicy.h/.cpp` — 上下文裁剪策略，调整 `maxMessages` / `maxTotalSize` 等参数 |
| 界面重复代码想复用 | 先看 `project/confirmdialogs.h` / `ui/uiconstants.h` / `data/appsettings.h` / `ui/conversationview.h` / `core/contextpolicy.h` 里有没有现成的，见「三、6」和「三、7」 |

---

## 十、常见问题

### Q: 如何添加新的工具？

见「九」表格里的对应行。简要版：
1. `tool_executor.cpp` 的 `toolDefinitions()` 加 JSON Schema
2. 写执行函数，`execute()` 里注册路由
3. 如果是写操作/危险操作，`isWriteTool()` 返回 true，并在 `previewDiff()` 里加对应分支

### Q: 索引为什么不包含所有代码行？

token 预算有限。索引只保存"骨架信息"（语言、框架、类名、文件名、行数），具体代码内容仍然通过 `read_file` 按需读取。

### Q: Chat 模式和 Project 模式如何切换？

通过 ElaWindow 的导航切换。`eventFilter` 监听 `ProjectPage` 的 `Show`/`Hide` 事件，调用 `ModeManager::enterProjectMode()` / `enterChatMode()`。

### Q: 为什么改了 `.cpp`/`.h` 才需要重新编译，改 `resources/` 下的 md 文件不用？

简单说：会被"编译"进程序内部的东西（源码），改了要重新编译；程序运行时从磁盘读取的外部文件（`app/resources/*.md`、`app/avatar/*.png`），改了直接生效，不用重新编译。

### Q: 有没有不需要纠结的问题？

1. 不需要关注图片路径为什么不用 qrc——用 qrc 图片会加载不出来，原因不明，保持现状即可
2. 编译时的一些警告基本不用在意

---

> 最后更新：2026.8.2（新增双模型配置、设置页卡片化、底部状态栏、代码块浅色主题）（新增 ContextPolicy 上下文裁剪 + ConversationView 对话展示共享组件）
> 建议在 AI 读取此项目时，先读本文件的一~三节，遇到具体问题查第九节表格，再进对应源文件。
