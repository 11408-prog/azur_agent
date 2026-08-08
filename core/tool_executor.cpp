#include "core/tool_executor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

QStringList ToolExecutor::s_allowedPaths;

namespace {

constexpr qint64 kMaxReadFileSize = 300 * 1024; // 单文件最大 300KB
constexpr int kMaxDirEntries = 200;             // 单次列目录最多返回的条目数

// 简单粗略地判断是否为二进制文件：抽样检查前若干字节里有没有 NUL 字符
bool looksBinary(const QByteArray &data)
{
    const int sampleSize = qMin(data.size(), 4096);
    for (int i = 0; i < sampleSize; ++i) {
        if (data.at(i) == '\0') {
            return true;
        }
    }
    return false;
}

} // namespace

QJsonArray ToolExecutor::toolDefinitions()
{
    qDebug()<<"[TOOL_EXEC] toolDefinitions 被调用";
    QJsonArray tools;

    // ---- read_file ----
    {
        QJsonObject pathProp;
        pathProp["type"] = "string";
        pathProp["description"] = "相对于工作区根目录的文件路径，例如 src/mainwindow.cpp";

        QJsonObject props;
        props["path"] = pathProp;

        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "path" };

        QJsonObject func;
        func["name"] = "read_file";
        func["description"] =
            "读取工作区内某个文本文件的完整内容。仅限工作区目录内的路径，"
            "单文件最大 300KB，超出大小或疑似二进制文件会被拒绝。";
        func["parameters"] = params;

        QJsonObject tool;
        tool["type"] = "function";
        tool["function"] = func;
        tools.append(tool);
    }

    // ---- list_directory ----
    {
        QJsonObject pathProp;
        pathProp["type"] = "string";
        pathProp["description"] = "相对于工作区根目录的目录路径，传空字符串或 \".\" 表示工作区根目录本身";

        QJsonObject props;
        props["path"] = pathProp;

        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "path" };

        QJsonObject func;
        func["name"] = "list_directory";
        func["description"] =
            "列出工作区内某个目录下的文件和子目录（只列一层，不递归）。仅限工作区目录内的路径。";
        func["parameters"] = params;

        QJsonObject tool;
        tool["type"] = "function";
        tool["function"] = func;
        tools.append(tool);
    }

    return tools;
}

// 目前工具集里只有只读工具（read_file / list_directory），
// 没有任何写操作，因此这里恒返回 false，AgentEngine::writeConfirmationRequired
// 也就永远不会触发。保留这个函数只是为了让调用方（AgentEngine）不用改接口。
bool ToolExecutor::isWriteTool(const QString &toolName)
{
    Q_UNUSED(toolName);
    return false;
}

void ToolExecutor::setAllowedPaths(const QStringList &paths)
{
    qDebug()<<"[TOOL_EXEC] setAllowedPaths | 路径数="<<paths.size();
    s_allowedPaths = paths;
}

// 把"相对路径"解析成绝对路径，并确保结果没有越出 workspaceRoot 范围（防路径穿越）
QString ToolExecutor::resolveSafePath(const QString &workspaceRoot, const QString &relativePath, bool *ok)
{
    *ok = false;

    QDir rootDir(workspaceRoot);
    if (!rootDir.exists()) {
        qDebug() << "[TOOL_EXEC] resolveSafePath 工作区目录不存在";
        return QString();
    }
    const QString canonicalRoot = rootDir.canonicalPath();
    if (canonicalRoot.isEmpty()) {
        qDebug() << "[TOOL_EXEC] resolveSafePath 无法获取规范路径";
        return QString();
    }

    QString rel = relativePath.trimmed();
    if (rel.isEmpty()) {
        rel = ".";
    }

    const QString combined = QDir(canonicalRoot).filePath(rel);
    const QString cleaned = QDir::cleanPath(combined);

    QFileInfo info(cleaned);
    const QString canonicalTarget = info.exists() ? info.canonicalFilePath() : cleaned;

    // 判断 target 是否在 dir 内部（含 dir 本身）
    // 使用 relativeFilePath 跨平台判断，不再依赖 "/" 字符串拼接
    auto isInside = [](const QString &dir, const QString &target) -> bool {
        if (target == dir) return true;
        QDir d(dir);
        QString relPath = d.relativeFilePath(target);
        if (relPath == ".") return true;
        // 如果 relativeFilePath 返回的还是绝对路径，说明跨盘符或无法相对化，判定为越界
        if (QDir::isAbsolutePath(relPath)) return false;
        // 以 "../" 开头或就是 ".." → 在工作区外
        // 注意："..foo" 这种合法文件名不会命中（不以 "/" 结尾）
        if (relPath.startsWith("../") || relPath == "..") return false;
        return true;
    };

    if (isInside(canonicalRoot, canonicalTarget)) {
        *ok = true;
        return canonicalTarget;
    }

    // 不在根目录内，检查白名单（使用同样的跨平台安全判断）
    for (const QString &allowed : s_allowedPaths) {
        const QString canonicalAllowed = QDir(allowed).canonicalPath();
        if (canonicalAllowed.isEmpty()) continue;

        if (isInside(canonicalAllowed, canonicalTarget)) {
            *ok = true;
            return canonicalTarget;
        }
    }

    qDebug() << "[TOOL_EXEC] 路径越界被拒绝 | rel=" << relativePath
             << "|canonicalTarget=" << canonicalTarget;
    return QString();
}

QString ToolExecutor::readFile(const QString &workspaceRoot, const QJsonObject &args, bool *ok, QString *displayLabel)
{
    *ok = false;
    const QString relPath = args.value("path").toString();
    *displayLabel = QString("读取 %1").arg(relPath.isEmpty() ? QStringLiteral("(空路径)") : relPath);

    qDebug()<<"[TOOL_EXEC] readFile | relPath="<<relPath;

    if (relPath.isEmpty()) {
        return QStringLiteral("错误：未提供 path 参数");
    }

    bool pathOk = false;
    const QString fullPath = resolveSafePath(workspaceRoot, relPath, &pathOk);
    if (!pathOk) {
        return QString("错误：路径 \"%1\" 不在工作区目录内，已拒绝访问").arg(relPath);
    }

    QFileInfo info(fullPath);
    if (!info.exists()) {
        return QString("错误：文件不存在: %1").arg(relPath);
    }
    if (info.isDir()) {
        return QString("错误：\"%1\" 是一个目录，请改用 list_directory").arg(relPath);
    }
    if (info.size() > kMaxReadFileSize) {
        return QString("错误：文件过大（约 %1 KB），超过 300KB 上限，无法读取").arg(info.size() / 1024);
    }

    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString("错误：无法打开文件: %1").arg(relPath);
    }
    const QByteArray data = file.readAll();
    file.close();

    if (looksBinary(data)) {
        return QString("错误：\"%1\" 看起来是二进制文件，无法作为文本读取").arg(relPath);
    }

    *ok = true;
    const QString text = QString::fromUtf8(data);
    *displayLabel = QString("读取 %1 (%2 行)").arg(relPath).arg(text.count('\n') + 1);
    qDebug()<<"[TOOL_EXEC] readFile 成功 | relPath="<<relPath<<"|行数="<<(text.count('\n')+1);
    return text;
}

QString ToolExecutor::listDirectory(const QString &workspaceRoot, const QJsonObject &args, bool *ok, QString *displayLabel)
{
    *ok = false;
    QString relPath = args.value("path").toString().trimmed();
    if (relPath.isEmpty()) {
        relPath = ".";
    }
    *displayLabel = QString("列出目录 %1").arg(relPath);

    qDebug()<<"[TOOL_EXEC] listDirectory | relPath="<<relPath;

    bool pathOk = false;
    const QString fullPath = resolveSafePath(workspaceRoot, relPath, &pathOk);
    if (!pathOk) {
        return QString("错误：路径 \"%1\" 不在工作区目录内，已拒绝访问").arg(relPath);
    }

    QFileInfo info(fullPath);
    if (!info.exists() || !info.isDir()) {
        return QString("错误：\"%1\" 不是一个有效目录").arg(relPath);
    }

    QDir dir(fullPath);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);

    QStringList lines;
    int count = 0;
    for (const QFileInfo &entry : entries) {
        if (count >= kMaxDirEntries) {
            lines << QString("... 还有更多条目未显示（超过 %1 个上限）").arg(kMaxDirEntries);
            break;
        }
        if (entry.isDir()) {
            lines << QString("[目录] %1/").arg(entry.fileName());
        } else {
            lines << QString("[文件] %1 (%2 字节)").arg(entry.fileName()).arg(entry.size());
        }
        ++count;
    }

    *ok = true;
    *displayLabel = QString("列出目录 %1 (%2 项)").arg(relPath).arg(count);
    return lines.isEmpty() ? QStringLiteral("(空目录)") : lines.join('\n');
}

QString ToolExecutor::execute(const QString &workspaceRoot, const QString &toolName,
                              const QJsonObject &arguments, bool *ok, QString *displayLabel,
                              QString *diffOutput)
{
    qDebug()<<"[TOOL_EXEC] execute | toolName="<<toolName;
    if (diffOutput) diffOutput->clear();

    if (workspaceRoot.isEmpty() || !QDir(workspaceRoot).exists()) {
        *ok = false;
        *displayLabel = QStringLiteral("工作区目录无效");
        return QStringLiteral("错误：工作区目录不存在，请先在设置页选择一个有效目录");
    }

    if (toolName == QLatin1String("read_file")) {
        return readFile(workspaceRoot, arguments, ok, displayLabel);
    }
    if (toolName == QLatin1String("list_directory")) {
        return listDirectory(workspaceRoot, arguments, ok, displayLabel);
    }

    *ok = false;
    *displayLabel = QString("未知工具: %1").arg(toolName);
    return QString("错误：不支持的工具名称: %1").arg(toolName);
}
