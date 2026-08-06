#ifndef TOOL_EXECUTOR_H
#define TOOL_EXECUTOR_H

#include <QString>
#include <QJsonArray>
#include <QJsonObject>

// 工具执行器：read_file / list_directory / search_file_content / write_file / apply_patch
// 所有路径都会被强制限制在"工作区目录"内，越权访问一律拒绝。
class ToolExecutor
{
public:
    // 返回工具的 JSON Schema 定义（OpenAI/DeepSeek 的 function calling 格式），
    // 直接塞进请求体的 "tools" 字段。
    static QJsonArray toolDefinitions();

    // 执行一次工具调用。
    //   workspaceRoot : 工作区根目录（设置页选择的目录）
    //   toolName      : 工具名，如 "read_file" / "write_file"
    //   arguments     : 已经解析好的参数对象
    //   ok            : [出参] 是否执行成功
    //   displayLabel  : [出参] 用于步骤指示器展示的简短描述，如 "读取 src/main.cpp (120行)"
    //   diffOutput    : [出参] 如果是写操作，返回人类可读的 diff 描述用于确认弹窗；读操作返回空
    // 返回值会原样作为 role:"tool" 消息的 content 回传给模型。
    static QString execute(const QString &workspaceRoot, const QString &toolName,
                            const QJsonObject &arguments, bool *ok, QString *displayLabel,
                            QString *diffOutput = nullptr);

    // 设置额外允许访问的路径白名单（配合 allowed_paths 使用）
    static void setAllowedPaths(const QStringList &paths);

    // 判断某个工具是否涉及写操作（需要用户确认）
    static bool isWriteTool(const QString &toolName);

    // 判断某个命令是否被列入黑名单（禁止执行）
    static bool isBlacklistedCommand(const QString &command, QString *reason = nullptr);

    // 预览写操作的结果（不实际修改文件），返回人类可读的 diff 描述
    static QString previewDiff(const QString &workspaceRoot, const QString &toolName,
                                const QJsonObject &arguments, bool *ok, QString *displayLabel);

private:
    static QString resolveSafePath(const QString &workspaceRoot, const QString &relativePath, bool *ok);
    static QString readFile(const QString &workspaceRoot, const QJsonObject &args, bool *ok, QString *displayLabel);
    static QString listDirectory(const QString &workspaceRoot, const QJsonObject &args, bool *ok, QString *displayLabel);
    static QString searchFileContent(const QString &workspaceRoot, const QJsonObject &args, bool *ok, QString *displayLabel);
    static QString writeFile(const QString &workspaceRoot, const QJsonObject &args, bool *ok, QString *displayLabel, QString *diffOutput);
    static QString applyPatch(const QString &workspaceRoot, const QJsonObject &args, bool *ok, QString *displayLabel, QString *diffOutput);
    static QString generateDiff(const QString &originalContent, const QString &newContent,
                                 const QString &relPath);
    static QString runCommand(const QString &workspaceRoot, const QJsonObject &args, bool *ok, QString *displayLabel);

    // 把 apply_patch 的"逐条 search/replace"应用逻辑抽成公共函数，
    // 供 applyPatch()（真正执行）和 previewDiff()（确认弹窗预览）共用，
    // 避免两处各写一份、逻辑跑偏导致"预览看到的"和"实际发生的"不一致。
    // 成功返回 true 并把结果写入 *outContent；*resultsOut 记录每条 patch 的执行说明（成功/模糊匹配/失败原因）。
    // 任意一条 patch 匹配失败（找不到或有歧义）都会导致整体失败（返回 false），不做部分应用。
    static bool applyPatchesToContent(const QString &originalContent, const QJsonArray &patches,
                                       QString *outContent, QStringList *resultsOut,
                                       QString *failureMessage);

    static QStringList s_allowedPaths;
};

#endif // TOOL_EXECUTOR_H
