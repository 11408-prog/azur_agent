# Azur Agent 项目说明

> 基于 Qt6 + ElaWidgetTools 构建的 AI 软件开发助手。
> 从"AI 聊天助手"向"AI 软件工程代理"演进中。

---

## 一、项目概览

### 目录结构

```
agent_/
├── CMakeLists.txt                  # 构建配置
├── app.qrc                         # Qt 资源文件
│
├── main.cpp                        # 入口：日志钩子 + ElaApplication 初始化
│
├── mainwindow.h/.cpp               # UI 主框架：导航、模式切换、AppBar
│
├── chatpagewidget.h/.cpp           # 聊天模式页面（已从 MainWindow 解耦）
├── messagebubblewidget.h/.cpp      # 消息气泡组件（用户/AI 通用）
├── markdownrenderer.h/.cpp         # Markdown → HTML 渲染器
│
├── projectpage.h/.cpp              # 项目模式页面：三栏布局
├── activitypanel.h/.cpp            # AI 活动步骤展示面板
├── project_analyzer.h/.cpp         # 项目索引分析器
├── projectsession.h/.cpp           # 项目会话持久化（.azur/project.json）
│
├── settingpagewidget.h/.cpp        # 设置页面
├── conversationmanager.h/.cpp      # 对话管理（创建/保存/加载/删除）
├── projectconversationservice.h/.cpp # 项目对话服务层（已从 MainWindow 解耦）
├── projectconvdialog.h/.cpp        # 项目对话列表对话框
│
├── ai_client.h/.cpp                # AI API 客户端（流式 + Function Calling）
├── agent_engine.h/.cpp             # Agent 循环引擎
├── tool_executor.h/.cpp            # 文件操作工具执行器
├── promptloader.h/.cpp             # System Prompt 构建器
│
├── resources/                      # Prompt 资源文件
│   ├── agent_core.md               # Agent 工作准则
│   ├── enterprise_personality.md   # 企业人格模型
│   ├── enterprise_quotes.md        # 台词参考
│   └── 碧蓝航线 企业.md             # 角色背景设定
│
├── avatar/                         # 头像图片
├── lib/ElaWidgetTools/             # 预编译的 ElaWidgetTools 库
├── build/                          # 构建输出
├── daily_log/                      # 运行日志
└── .azur/                          # 项目缓存目录
    ├── project.json                # 项目会话配置
    └── project_index.json          # 项目索引缓存 ★ 自动生成
```

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
┌─────────────────────────────────────────────────────────┐
│  MainWindow (ElaWindow)                                 │
│  ┌───────────────────────────────────────────────────┐  │
│  │  Navigation: 对话 | 项目 | 设置 | 关于             │  │
│  │  AppBar: [打开文件夹] [对话列表] [历史记录] [侧栏]  │  │
│  └────────┬──────────────────────────┬───────────────┘  │
│           │                          │                  │
│  ┌────────▼────────┐    ┌───────────▼────────────┐     │
│  │  Chat Mode      │    │  Project Mode           │     │
│  │  ChatPageWidget │    │  ProjectPage            │     │
│  │  ┌───────────┐  │    │  ┌────┬──────┬──────┐  │     │
│  │  │侧栏│ 对话   │  │    │  │文件│ Agent│ 活动 │  │     │
│  │  │历史│ 区域   │  │    │  │树  │ 对话 │ 面板 │  │     │
│  │  └───────────┘  │    │  └────┴──────┴──────┘  │     │
│  └────────┬────────┘    └───────────┬────────────┘     │
│           │                          │                  │
│           └──────────┬───────────────┘                  │
│                      │                                  │
│              ┌───────▼────────┐                         │
│              │  AgentEngine   │                         │
│              │  (AI 请求循环)  │                        │
│              └──┬─────────┬───┘                         │
│                 │         │                             │
│        ┌────────▼──┐  ┌──▼──────────┐                  │
│        │ AI Client │  │ ToolExecutor│                  │
│        │ (流式API)  │  │ (文件+命令) │                  │
│        └───────────┘  └─────────────┘                  │
│                                                         │
│  ┌────────────────────┐  ┌──────────────────────┐       │
│  │ ConversationManager│  │  ProjectAnalyzer     │       │
│  │ (对话持久化)        │  │  (项目索引)         │       │
│  └────────────────────┘  └──────────────────────┘       │
│                                                         │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────┐    │
│  │ PromptLoader │  │ Markdown     │  │ Message-   │    │
│  │ (Prompt构建)  │  │ Renderer     │  │ Bubble     │    │
│  └──────────────┘  └──────────────┘  │ Widget     │    │
│                                      └────────────┘    │
└─────────────────────────────────────────────────────────┘
```

---

## 三、文件详解

### 3.1 入口层

#### `main.cpp` (71 行)

程序入口。职责：

1. **日志钩子** — 将所有 `qDebug()`/`qWarning()` 输出重定向到 `daily_log/azur_debug_YYYY-MM-DD.log`
2. **ElaApplication 初始化** — ElaWidgetTools 库的要求
3. **创建并显示 MainWindow**

```cpp
// 日志文件路径: agent_/daily_log/azur_debug_2024-01-01.log
// 按天拆分，方便排查问题
```

---

### 3.2 UI 主框架

#### `mainwindow.h/.cpp` (917 行)

核心 UI 框架，继承 `ElaWindow`。负责 UI 协调和模式切换，项目对话的业务逻辑已委托给 `ProjectConversationService`。

**导航管理：**
- 4 个页面节点：对话、项目、设置、关于
- 通过 `addPageNode()` / `addFooterNode()` 构建侧边导航
- 通过 `eventFilter` 监听 ProjectPage 的显示/隐藏，自动切换模式

**双模式切换：**
- `AgentMode::Chat` — 普通 AI 对话（带侧栏历史）
- `AgentMode::Project` — 项目开发模式（三栏布局）
- 切换时保存/恢复对话状态、切换工具栏按钮

**AppBar 右侧按钮区：**
- `sidebarToggleBtn_` — 折叠左侧面板（Chat 模式折叠侧栏，Project 模式折叠文件树）
- `openFolderBtn_` — 打开项目文件夹（仅 Project 模式可见）
- `projectConvListBtn_` — 打开项目对话列表对话框，显示当前项目的所有对话，支持打开/删除（仅 Project 模式可见）
- `projectHistoryBtn_` — 项目历史记录对话框，显示最近打开的项目列表（仅 Project 模式可见）

**关键依赖：**
```
MainWindow
├── ChatPageWidget         (聊天页面)
├── ProjectPage            (项目页面)
├── SettingPageWidget      (设置页面)
├── AgentEngine            (AI 引擎，Chat 模式使用)
├── ConversationManager    (对话持久化)
├── ProjectConversationService (项目对话服务层)
└── DeepSeekClient         (API 客户端)
```

**数据流（发送消息）：**
```
用户输入 → onSendClicked()
  → messageHistory_.append(userMsg)
  → chatEngine_->start(...)       // 启动 AgentEngine
  → onApiChunkReceived()         // 流式输出
  → onApiResponseCompleted()     // 完成 → 持久化
```

---

### 3.3 聊天模式

#### `chatpagewidget.h/.cpp` (697 行)

聊天页面的独立组件。从 MainWindow 解耦而来。

**布局：**
```
┌──────────────┬──────────────────────────────┐
│  侧栏(历史)   │         消息区域               │
│  QListWidget  │  ElaScrollArea              │
│              │   ┌──────────────────────┐   │
│  清空按钮     │   │  MessageBubbleWidget  │   │
│              │   │  MessageBubbleWidget  │   │
│              │   │  ...                  │   │
│              │   └──────────────────────┘   │
│              │  ┌──────────────────────┐    │
│              │  │ 输入框 + 发送按钮     │    │
│              │  └──────────────────────┘    │
└──────────────┴──────────────────────────────┘
```

**侧栏功能：**
- 动画折叠/展开（`kSidebarExpandedWidth = 260`）
- 历史对话列表 + 右键菜单（重命名/删除）
- 清空历史按钮

**接收的信号（来自 AgentEngine）：**
- `chunkReceived` → `onChunkReceived()` → 流式追加到当前气泡
- `finished` → `onResponseCompleted()` → 完成渲染
- `stepChanged` → `updateAiStep()` → 步骤指示器

---

### 3.4 项目模式

#### `projectpage.h/.cpp` (669 行)

项目开发模式的三栏布局页面。

**布局：**
```
┌──────────────┬────────────────────────┬──────────────┐
│  左侧面板     │      中间面板           │  右侧面板     │
│  (220px)     │     Agent 对话          │  (250px)     │
│              │                        │              │
│  项目文件树   │  MessageBubbleWidget   │  AI 活动     │
│  QTreeView   │  MessageBubbleWidget   │  面板        │
│  +           │  ...                   │  Activity-   │
│  QFileSystem │                        │  Panel       │
│  Model       │  ┌────────────────┐    │              │
│              │  │ 输入框 + 发送   │    │              │
│              │  └────────────────┘    │              │
└──────────────┴────────────────────────┴──────────────┘
```

**关键特性：**
- 左右面板可折叠（带动画，状态持久化到 QSettings）
- 文件树自动跟随工作区路径
- 项目路径变更时自动重建索引（`rebuildIndex()`）

**数据流（发送消息）：**
```
用户输入 → onSendClicked()
  → 从 QSettings 读取 API 配置
  → messageHistory_.append(userMsg)
  → engine_->start(... systemPrompt_ + indexSummary_, ToolExecutor::toolDefinitions(), ...)
     ↑ systemPrompt 已附带项目索引摘要
  → onChunkReceived() / onResponseCompleted() / onError()
  → 同步 messageHistory_ 并 emit conversationUpdated()
```

**与 ChatPageWidget 核心区别：**
- 没有历史侧栏，换成文件树 + 活动面板
- 使用 `ToolExecutor::toolDefinitions()` 注册文件工具
- system prompt 末尾附带项目索引摘要
- 启动时自动重建项目索引

---

### 3.5 Agent 引擎

#### `agent_engine.h/.cpp` (303 行)

Agent 循环核心，封装了"请求 AI → 解析工具调用 → 执行工具 → 再次请求"的完整循环。

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
                           有写操作            无写操作
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
- `messageHistory_` — 完整消息历史（含 tool 消息），外部可通过 `messageHistory()` 获取
- `toolRound_` / `maxToolRounds_` — 防止无限循环（默认上限 200 轮）
- `waitingConfirm_` — 写操作等待用户确认状态
- `pendingToolCalls_` / `pendingDiffs_` — 暂存的待确认工具调用

**信号：**
| 信号 | 触发时机 |
|------|----------|
| `chunkReceived(delta)` | 流式文本片段 |
| `stepChanged(text)` | 步骤状态变化 |
| `finished(fullText)` | 循环正常结束 |
| `errorOccurred(msg)` | 错误终止 |
| `writeConfirmationRequired(diffList)` | 需要用户确认写操作 |

---

#### `ai_client.h/.cpp` (324 行)

AI API 客户端，负责与 OpenAI/DeepSeek 兼容接口通信。

**核心功能：**
- `sendMessage()` — POST 请求，支持 `stream: true` + `tools`
- `testConnection()` — GET `/models` 测试连接
- `cancel()` — 中断当前请求

**SSE 流式处理关键设计：**
- `sseLineBuffer_` — 持久化缓冲区，处理网络分包导致的不完整行
- `PendingToolCall` — 按 `index` 累积拼接工具调用分片（`arguments` 是分段 JSON 字符串）
- `sawToolCallFinish_` — 检测 `finish_reason: "tool_calls"`
- **兜底逻辑** — 非流式响应也兼容处理

**错误处理：**
- 401/403 → "认证失败"
- 404 → "接口地址不可用"
- HostNotFound → "无法找到服务器"
- ConnectionRefused → "服务器拒绝连接"
- 服务端 error.message → 直接透传

---

#### `tool_executor.h/.cpp` (约 920 行)

文件操作与命令执行器。

**五个工具：**
| 工具 | 功能 | 安全限制 |
|------|------|----------|
| `read_file` | 读取文本文件内容 | ≤300KB，跳过二进制 |
| `list_directory` | 列出目录内容 | ≤200 条 |
| `write_file` | 创建/覆盖写入文件 | ≤1MB，自动创建父目录 |
| `apply_patch` | 搜索-替换局部修改 | ≤20 个 patch/次 |
| `run_command` | 在项目目录下执行终端命令 | 30s 超时，高危命令黑名单 |

**安全设计：**
- `resolveSafePath()` — 路径穿越防护，所有路径必须在工作区目录内
- `looksBinary()` — 抽样检测 NUL 字节，拒绝二进制文件
- `s_allowedPaths` — 额外白名单路径

**apply_patch 亮点：**
- 精确匹配失败后自动尝试 `findFuzzyLineMatch()`（忽略行首尾空白）
- 匹配不唯一时报错，防止误改
- 逐 patch 应用，中途失败立即回滚

**Diff 生成：**
- 基于 LCS 动态规划，正确计算插入/删除/保留行
- 超过 400 万 dp cell 阈值时跳过（防卡顿），给摘要提示
- 上下文行数控制（`kContextLines = 2`），减少噪音

---

### 3.6 项目索引 ★ 新增

#### `project_analyzer.h/.cpp` (703 行)

离线项目结构分析器，让 AI 在开始工作前就知道项目全貌。

**核心流程：**
```
buildIndex(workspaceRoot)
  → scanDirectory() 递归扫描，跳过 build/ .git/ lib/ 等
  → 按扩展名分析文件内容（C++ / Python / 其他）
  → 提取 includes / classes / functions
  → detectLanguage() / detectFramework()
  → 生成完整 JSON 索引

needsRebuild(workspaceRoot)
  → 对比每个文件的 lastModified 与缓存的时间戳
  → 全部一致 → 返回 false（可复用缓存）

generateSummary(index)
  → 从 JSON 索引生成 Markdown 摘要
  → 类列表（含文件路径、继承关系、方法数）
  → 关键文件（按行数排序，显示类名）
```

**索引缓存位置：** `.azur/project_index.json`

**C++ 分析（正则提取）：**
- `#include <...>` / `#include "..."` → includes
- `class Xxx` / `struct Xxx` → classes
- `返回类型 函数名(参数) {` 或 `;` → functions
- 过滤掉 `if`/`for`/`while`/lambda 等误匹配
- 大文件（>300KB）只扫头尾各 100KB

**框架检测：**
- 检查 `CMakeLists.txt` → find_package(Qt → Qt6/Qt5
- 检查 `package.json` → Node.js
- 检查 `Cargo.toml` → Rust/Cargo
- 检查 `go.mod` → Go

---

### 3.7 UI 组件

#### `messagebubblewidget.h/.cpp` (242 行)

消息气泡组件，同时支持用户消息和 AI 消息。

**两种显示模式：**
| 模式 | 用户消息 | AI 消息 |
|------|----------|---------|
| 内容 | `ElaText` 纯文本 | `QTextBrowser` Markdown HTML |
| 头像 | user.png | bot.png |
| 对齐 | 右对齐 | 左对齐 |

**AI 消息特性：**
- Markdown 渲染（代码块带"复制"按钮 + 红绿灯装饰）
- 流式内容实时更新（`setAiStreamingContent()`）
- 步骤指示器（Chat 模式使用）

#### `activitypanel.h/.cpp` (225 行)

AI 活动步骤展示面板，实时显示 Agent 当前执行状态。

**三种状态及图标：**
| 状态 | 图标 | 颜色 |
|------|------|------|
| Pending | 旋转动画 ⠋⠙⠹... | 蓝色 `#4a9eff` |
| Completed | ✓ | 绿色 `#3fb950` |
| Failed | ✗ | 红色 `#f85149` |

**输入解析：**
```
onStepChanged("正在连接...")      → addPendingActivity()
onStepChanged("✓ 读取完成")       → completeLastPending()
onStepChanged("✗ 文件不存在")     → failLastPending()
```

#### `markdownrenderer.h/.cpp` (113 行)

轻量级 Markdown → HTML 转换器，支持：
- 代码块（带语言标识、复制按钮、红绿灯圆点）
- 行内代码
- **加粗** / *斜体*
- [链接](url)
- Diff 语法高亮（`+` 绿色 / `-` 红色）
- `adjustTextBrowserHeight()` — 自适应内容高度

#### `settingpagewidget.h/.cpp` (271 行)

设置页面组件，管理：
- API Key（ElaLineEdit）
- Base URL（ElaLineEdit，默认 `https://api.deepseek.com`）
- 模型名称（ElaComboBox + 最近使用自动补全）
- 背景透明度（QSlider）
- 连接测试按钮

#### `projecthistorydialog.h/.cpp`

项目历史记录对话框。从 `QSettings` 读取最近打开的项目列表，支持选择项目并切换到对应的对话。

#### `projectconvdialog.h/.cpp`

当前项目对话列表对话框。展示当前项目下所有对话（标题 + 更新时间 + 消息数量），
支持双击打开、选中打开、选中删除等操作，替代了旧版的 QMenu 下拉菜单。

---

### 3.8 数据管理层

#### `conversationmanager.h/.cpp` (217 行)

对话持久化管理器。

**存储结构：**
```
{AppData}/AzurAgent/data/
├── conversations.json       # 元信息（id / title / created / updated / messageCount）
└── chats/
    ├── 20240101-120000-001.json   # 消息内容
    ├── 20240101-123000-002.json
    └── ...
```

**操作接口：**
- `createNewConversation(title)` → 返回新 ID
- `saveConversation(id, messages, title)` → 保存消息 + 更新元信息
- `loadConversation(id)` → 加载消息数组
- `deleteConversation(id)` → 删除文件 + 更新元信息
- `renameConversation(id, title)` → 重命名
- `conversationsMeta()` → 按更新时间降序排列的元信息列表

#### `projectsession.h/.cpp` (66 行)

项目会话数据，保存在项目目录下的 `.azur/project.json`。

```json
{
    "projectPath": "C:/path/to/project",
    "allowedPaths": []
}
```

#### `projectconversationservice.h/.cpp`

项目对话管理服务层。从 `MainWindow` 解耦而来，封装了项目对话的保存、加载、切换、迁移等业务逻辑。

**核心方法：**
| 方法 | 功能 |
|------|------|
| `saveConversation(convId, messages, projectPath)` | 保存项目对话到共享存储 |
| `loadConversation(convId)` | 加载指定对话的消息 |
| `conversationsForProject(projectPath)` | 获取项目下的对话列表 |
| `resolveConversation(projectPath, preferredConvId)` | 解析/创建对话（含迁移逻辑） |
| `saveProjectEntry(projectPath, convId, convTitle)` | 静态方法，写 QSettings 历史 |
| `findEntryConversationId(projectPath)` | 静态方法，从 QSettings 历史查找 |
| `migrateOldConversations()` | 一次性迁移旧版项目对话 |
| `deleteConversation(id)` | 删除对话 |
| `conversationTitle(convId)` | 获取对话标题 |

#### `promptloader.h/.cpp` (32 行)

构建 system prompt，从 `resources/` 目录依次加载：
1. `agent_core.md` — Agent 工作准则
2. `enterprise_personality.md` — 企业人格模型
3. `enterprise_quotes.md` — 台词参考
→ 拼接为一个完整的 system prompt

---

## 四、数据流

### 4.1 Chat 模式

```
用户输入
  │
  ▼
MainWindow::onSendClicked()
  │ 读取 API 配置（QSettings）
  │ messageHistory_.append(userMsg)
  ▼
AgentEngine::start(apiKey, baseUrl, model, messageHistory, systemPrompt, tools=[], workspaceRoot="")
  │
  ├── sendRequest()
  │     │
  │     ▼
  │   DeepSeekClient::sendMessage()
  │     │ POST {model, messages, stream:true}
  │     ▼
  │   AI 流式返回
  │     │
  │     ├── chunkReceived(delta) → MainWindow::onApiChunkReceived()
  │     │                           → ChatPageWidget::onChunkReceived()
  │     │                           → MessageBubbleWidget::setAiStreamingContent()
  │     │
  │     └── responseCompleted(fullText) → MainWindow::onApiResponseCompleted()
  │                                        → ChatPageWidget::onResponseCompleted()
  │                                        → ConversationManager::saveConversation()
  │
  └── 用户确认 → AgentEngine::confirmWrite()（Chat 模式 tool 为空，不触发此路径）
```

### 4.2 Project 模式

```
打开项目
  │
  ▼
ProjectPage::setProjectPath(path)
  │
  ├── QFileSystemModel 设置根路径 → 文件树刷新
  │
  └── rebuildIndex()
        │
        ├── needsRebuild() → 检查 .azur/project_index.json 缓存
        │   ├── 缓存有效 → loadIndex() → generateSummary()
        │   └── 缓存过期 → buildIndex() → saveIndex() → generateSummary()
        │
        ▼
      indexSummary_ 就绪

用户发送消息
  │
  ▼
ProjectPage::onSendClicked()
  │
  ├── loadSystemPrompt()  → PromptLoader::buildSystemPrompt() + indexSummary_
  │
  ├── engine_->start(apiKey, baseUrl, model, messageHistory,
  │                   systemPrompt_ + indexSummary_,
  │                   ToolExecutor::toolDefinitions(),
  │                   projectPath_)
  │
  ▼
AgentEngine 循环:
  │
  ├── sendRequest() → AI 返回
  │     │
  │     ├── 纯文本 → finished → onResponseCompleted()
  │     │
  │     └── tool_calls → onToolCallsReceived()
  │           │
  │           ├── 有写操作 → previewDiff() → writeConfirmationRequired()
  │           │   └── 用户确认 → confirmWrite(true) → executeToolCalls()
  │           │       └── 用户拒绝 → confirmWrite(false) → doFail()
  │           │
  │           └── 无写操作 → executeToolCalls()
  │                 │ 逐个执行 ToolExecutor::execute()
  │                 │ 结果追加到 messageHistory_
  │                 └── sendRequest() 继续下一轮
  │
  └── toolRound_ > maxToolRounds_ → doFail()
```

---

## 五、关键设计决策

### 1. 双模式架构

Chat 模式和 Project 模式共享同一个 `AgentEngine` 实例，但传入不同的参数：

| 参数 | Chat 模式 | Project 模式 |
|------|-----------|--------------|
| `tools` | `QJsonArray()` 空 | `ToolExecutor::toolDefinitions()` |
| `workspaceRoot` | `QString()` 空 | 项目路径 |
| `systemPrompt` | 人格 prompt | 人格 prompt + 项目索引摘要 |

这样设计避免了维护两套引擎实例，但代价是 Chat 模式也带了 writeConfirmation 的逻辑（虽然不会触发）。

### 2. 工具执行的安全模型

所有文件操作路径必须经过 `resolveSafePath()` 检查：
- 路径必须等于是工作区根目录或其子路径
- 或者在 `s_allowedPaths` 白名单中
- 路径遍历攻击（`../../`）会被拒绝

### 3. 写操作确认机制

写操作不会立即执行，而是：
1. `previewDiff()` 生成预览
2. 弹出 `ElaContentDialog` 展示 diff
3. 用户选择"接受"或"拒绝"
4. 拒绝时所有写工具标记为"已拒绝"返回给 AI

### 4. 项目索引的缓存策略

- 首次打开项目时全量扫描，保存到 `.azur/project_index.json`
- 再次打开时对比每个源文件的 `lastModified` 时间戳
- 全部一致 → 直接加载缓存（毫秒级）
- 有变更 → 重新扫描变更的文件（秒级）

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

## 七、开发路线（参考原设计文档）

| 阶段 | 目标 | 完成度 |
|------|------|--------|
| V1.5 | 文件操作（write_file / apply_patch / diff / 确认） | ✅ 已完成 |
| V2.0 | Agent Engine 重构 + 工具循环 | ✅ 基本完成 |
| V2.1 | 项目索引（ProjectAnalyzer） | ✅ **本次新增** |
| V2.5 | 命令执行（run_command） | ✅ 已完成 |
| V3.0 | Git 集成 + 代码解析（Tree-sitter） | ❌ 未开始 |
| V3.5 | 上下文管理（ContextManager） | ❌ 未开始 |
| V4.0 | 长期记忆 + 多 Agent 协作 | ❌ 未开始 |

---

## 八、类关系图

```
MainWindow (ElaWindow)
  ├── owns → DeepSeekClient (1)
  ├── owns → AgentEngine (1, 共享)
  ├── owns → ConversationManager (2: Chat + Project)
  ├── owns → ProjectConversationService (1)
  │     └── uses → ConversationManager (projectConvMgr_)
  ├── owns → ChatPageWidget (1)
  │     └── uses → MessageBubbleWidget (N)
  │           └── uses → MarkdownRenderer (静态)
  ├── owns → ProjectPage (1)
  │     ├── uses → AgentEngine (1, 共享)
  │     ├── uses → ProjectAnalyzer (静态)
  │     ├── uses → ToolExecutor (静态)
  │     ├── owns → ActivityPanel (1)
  │     └── uses → MessageBubbleWidget (N)
  ├── owns → SettingPageWidget (1)
  └── uses → ProjectSession (1)

ProjectConversationService
  └── uses → ConversationManager (projectConvMgr_, chatConvMgr_)

AgentEngine
  ├── owns → DeepSeekClient (1, 外部传入)
  └── uses → ToolExecutor (静态)

ConversationManager
  └── 持久化到 → {AppData}/AzurAgent/data/
```

---

## 九、常见问题

### Q: 如何添加新的工具？

1. 在 `tool_executor.cpp` 的 `toolDefinitions()` 中添加 JSON Schema 定义
2. 实现对应的执行函数（如 `myTool()`）
3. 在 `execute()` 中注册路由
4. 如果是写操作，在 `isWriteTool()` 和 `previewDiff()` 中添加支持

### Q: 索引为什么不包含所有代码行？

token 预算有限。索引只保存"骨架信息"（语言、框架、类名、文件名、行数），
让 AI 知道该读哪些文件。具体代码内容仍然通过 `read_file` 按需读取。

### Q: Chat 模式和 Project 模式如何切换？

通过 ElaWindow 的导航切换。`eventFilter` 监听 `ProjectPage` 的
`Show`/`Hide` 事件自动调用 `enterProjectMode()` / `enterChatMode()`。

---

> 最后更新：2026.7.26
> 建议在 AI 读取此项目时，先读 PROJECT_README.md，再按需深入各模块。
