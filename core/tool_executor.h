#ifndef TOOL_EXECUTOR_H
#define TOOL_EXECUTOR_H

#include <QString>
#include <QJsonArray>
#include <QJsonObject>

// 工具执行器：read_file / list_directory 两个只读工具。
// 所有路径都会被强制限制在"工作区目录"内，越权访问一律拒绝。
//
// 历史上这里还有 write_file / apply_patch / run_command / git_* 等写操作工具
// （连带黑名单检测、diff 预览、确认弹窗一整套安全机制），随着项目重心转向
// "和企业聊天"而不是"AI 编码 Agent"，这些已经移除，只保留最基础的只读能力。
class ToolExecutor
{
public:

    static QString resolveSafePath(const QString &workspaceRoot, const QString &relativePath, bool *ok);

    // 返回工具的 JSON Schema 定义（OpenAI/DeepSeek 的 function calling 格式），
    // 直接塞进请求体的 "tools" 字段。
    static QJsonArray toolDefinitions();

    // 执行一次工具调用。
    //   workspaceRoot : 工作区根目录（设置页选择的目录）
    //   toolName      : 工具名，如 "read_file" / "list_directory"
    //   arguments     : 已经解析好的参数对象
    //   ok            : [出参] 是否执行成功
    //   displayLabel  : [出参] 用于步骤指示器展示的简短描述，如 "读取 src/main.cpp (120行)"
    //   diffOutput    : 预留参数，当前没有写操作工具，恒为空
    // 返回值会原样作为 role:"tool" 消息的 content 回传给模型。
    static QString execute(const QString &workspaceRoot, const QString &toolName,
                            const QJsonObject &arguments, bool *ok, QString *displayLabel,
                            QString *diffOutput = nullptr);

    // 设置额外允许访问的路径白名单
    static void setAllowedPaths(const QStringList &paths);

    // 判断某个工具是否涉及写操作（需要用户确认）。当前恒返回 false。
    static bool isWriteTool(const QString &toolName);

    // ---- Python 后端（可选，默认关闭） ----
    // 开启后 execute() 通过 QProcess 调用 python/azur_tools/cli.py（stdio JSON 协议）
    // 执行工具；后端不可用（找不到虚拟环境 / 进程启动失败 / 超时 / 坏 JSON）时
    // 自动回退 C++ 原生实现并关闭开关。
    static void setUsePythonBackend(bool use);
    static bool usePythonBackend();
    // 显式指定 python 解释器 / cli.py 路径；不指定时自动从可执行文件目录向上
    // 回溯查找 azur_agent/Scripts/python.exe 与 python/azur_tools/cli.py。
    static void setPythonInterpreterPath(const QString &path);
    static void setPythonToolCliPath(const QString &path);

    // 解析（并缓存）Python 解释器与 cli.py 的路径。公开给 ttsclient 等
    // 复用同一套定位逻辑（TTS 脚本 tts_cli.py 与 cli.py 同目录）。
    static QString pythonInterpreterPath();
    static QString toolCliPath();

private:

    static QString readFile(const QString &workspaceRoot, const QJsonObject &args, bool *ok, QString *displayLabel);
    static QString listDirectory(const QString &workspaceRoot, const QJsonObject &args, bool *ok, QString *displayLabel);

    // 通过 Python 后端执行一次工具调用。backendAvailable 表示后端是否正常工作
    // （工具执行失败也是"正常工作的后端"，backendAvailable=true；进程起不来等
    // 基础设施故障才是 false）。
    static QString executeViaPython(const QString &workspaceRoot, const QString &toolName,
                                    const QJsonObject &arguments, bool *ok,
                                    QString *displayLabel, bool *backendAvailable);

    static bool s_usePythonBackend;
    static QString s_pythonInterpreter;
    static QString s_toolCliPath;
    static QStringList s_allowedPaths;
};

#endif // TOOL_EXECUTOR_H
