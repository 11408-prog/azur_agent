#include "tool_executor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QVector>
#include <QPair>
#include <QProcess>
#include <QDebug>

QStringList ToolExecutor::s_allowedPaths;

namespace {

constexpr qint64 kMaxReadFileSize = 300 * 1024; // 单文件最大 300KB
constexpr qint64 kMaxWriteFileSize = 1024 * 1024; // 写入上限 1MB
constexpr int kMaxDirEntries = 200;             // 单次列目录最多返回的条目数
constexpr int kMaxPatchCount = 20;               // 单次 apply_patch 最多 patch 数量

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

// 在原始文本 content 中，按"忽略每行首尾空白"的方式，为 searchText 寻找唯一匹配的行区间。
// 用于 apply_patch 精确匹配失败时的兜底：AI 生成的代码经常在缩进/行尾空白上跟文件本身有
// 细微出入，内容其实完全一致、只是格式对不上，这种情况不该直接判失败。
// 找到唯一匹配时通过 matchStart/matchLength 返回该区间在原始 content 里的字符范围（含原始
// 的空白/缩进），调用方用这个范围去替换——而不是把 search 参数里的空白格式强加进原文件。
// 找不到、或者匹配到不止一处，都返回 false（宁可报错也不猜）。
bool findFuzzyLineMatch(const QString &content, const QString &searchText,
                        int *matchStart, int *matchLength)
{
    const QStringList searchLines = searchText.split('\n');
    if (searchLines.isEmpty()) {
        return false;
    }

    QStringList contentLines;
    QVector<int> lineOffsets;
    int lineStart = 0;
    while (true) {
        const int nl = content.indexOf('\n', lineStart);
        if (nl == -1) {
            contentLines << content.mid(lineStart);
            lineOffsets << lineStart;
            break;
        }
        contentLines << content.mid(lineStart, nl - lineStart);
        lineOffsets << lineStart;
        lineStart = nl + 1;
    }

    QStringList normalizedSearch;
    for (const QString &l : searchLines) {
        normalizedSearch << l.trimmed();
    }

    const int searchLineCount = searchLines.size();
    QVector<int> matchStarts;
    for (int start = 0; start + searchLineCount <= contentLines.size(); ++start) {
        bool allMatch = true;
        for (int k = 0; k < searchLineCount; ++k) {
            if (contentLines[start + k].trimmed() != normalizedSearch[k]) {
                allMatch = false;
                break;
            }
        }
        if (allMatch) {
            matchStarts << start;
            if (matchStarts.size() > 1) {
                break; // 已经不唯一了，没必要继续找
            }
        }
    }

    if (matchStarts.size() != 1) {
        return false;
    }

    const int start = matchStarts.first();
    const int endLineIdx = start + searchLineCount - 1;
    *matchStart = lineOffsets[start];
    *matchLength = lineOffsets[endLineIdx] + contentLines[endLineIdx].size() - *matchStart;
    return true;
}

// 如果 content 超过 8000 字符，截断并追加说明
QString truncateForTool(const QString &content)
{
    constexpr int kMaxReturn = 8000;
    if (content.size() <= kMaxReturn) return content;
    return content.left(kMaxReturn) + QStringLiteral("\n\n... (内容过长，已截断至 %1 字符)").arg(kMaxReturn);
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

    // ---- write_file ----
    {
        QJsonObject pathProp;
        pathProp["type"] = "string";
        pathProp["description"] = "相对于工作区根目录的文件路径，例如 src/utils.cpp";

        QJsonObject contentProp;
        contentProp["type"] = "string";
        contentProp["description"] = "要写入的完整文件内容";

        QJsonObject props;
        props["path"] = pathProp;
        props["content"] = contentProp;

        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "path", "content" };

        QJsonObject func;
        func["name"] = "write_file";
        func["description"] =
            "创建新文件或完整覆盖写入已有文件。仅限工作区目录内的路径，"
            "写入内容上限 1MB。重要：这会完全覆盖目标文件，如果只想修改部分内容请改用 apply_patch。";
        func["parameters"] = params;

        QJsonObject tool;
        tool["type"] = "function";
        tool["function"] = func;
        tools.append(tool);
    }

    // ---- apply_patch ----
    {
        QJsonObject searchProp;
        searchProp["type"] = "string";
        searchProp["description"] = "要被替换的原始代码片段（必须在文件中精确且唯一匹配）";

        QJsonObject replaceProp;
        replaceProp["type"] = "string";
        replaceProp["description"] = "替换后的新代码片段";

        QJsonObject patchItem;
        patchItem["type"] = "object";
        QJsonObject patchProps;
        patchProps["search"] = searchProp;
        patchProps["replace"] = replaceProp;
        patchItem["properties"] = patchProps;
        patchItem["required"] = QJsonArray{ "search", "replace" };

        QJsonObject patchesProp;
        patchesProp["type"] = "array";
        patchesProp["description"] = "搜索-替换补丁列表，从上到下依次应用";
        patchesProp["items"] = patchItem;

        QJsonObject pathProp;
        pathProp["type"] = "string";
        pathProp["description"] = "相对于工作区根目录的文件路径，例如 src/mainwindow.cpp";

        QJsonObject props;
        props["path"] = pathProp;
        props["patches"] = patchesProp;

        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "path", "patches" };

        QJsonObject func;
        func["name"] = "apply_patch";
        func["description"] =
            "对工作区内的某个文件应用搜索-替换补丁。每个 patch 包含一段"
            "需要查找的原始文本（search）和替换后的新文本（replace）。"
            "search 必须在文件中精确且唯一匹配，否则拒绝执行。"
            "适用于局部修改代码，比 write_file 更安全。单次最多 20 个 patch。";
        func["parameters"] = params;

        QJsonObject tool;
        tool["type"] = "function";
        tool["function"] = func;
        tools.append(tool);
    }

    // ---- run_command ----
    {
        QJsonObject commandProp;
        commandProp["type"] = "string";
        commandProp["description"] =
            "要在终端中执行的命令，例如 \"npm test\" 或 \"python main.py\"。"
            "工作目录为项目根目录。命令超时时间为 30 秒。";

        QJsonObject props;
        props["command"] = commandProp;

        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "command" };

        QJsonObject func;
        func["name"] = "run_command";
        func["description"] =
            "在项目根目录下执行一条终端命令，并返回标准输出、标准错误和退出码。"
            "适用于运行构建、测试、代码检查、包管理、格式化等命令行工具。"
            "禁止执行高危操作（删除文件、格式化磁盘、修改权限等）。"
            "命令超时 30 秒，超时后自动终止。";
        func["parameters"] = params;

        QJsonObject tool;
        tool["type"] = "function";
        tool["function"] = func;
        tools.append(tool);
    }

    return tools;
}

bool ToolExecutor::isWriteTool(const QString &toolName)
{
    bool result = toolName == QLatin1String("write_file")
           || toolName == QLatin1String("apply_patch");
    qDebug()<<"[TOOL_EXEC] isWriteTool | toolName="<<toolName<<"|结果="<<result;
    return result;
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
        qDebug()<<"[TOOL_EXEC] resolveSafePath 工作区目录不存在";
        return QString();
    }
    const QString canonicalRoot = rootDir.canonicalPath();
    if (canonicalRoot.isEmpty()) {
        qDebug()<<"[TOOL_EXEC] resolveSafePath 无法获取规范路径";
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

    // 必须等于根目录本身，或者是根目录下的子路径，否则检查 allowedPaths
    if (canonicalTarget != canonicalRoot && !canonicalTarget.startsWith(canonicalRoot + "/")) {
        // 检查是否在额外允许路径白名单中
        for (const QString &allowed : s_allowedPaths) {
            const QString canonicalAllowed = QDir(allowed).canonicalPath();
            if (canonicalAllowed.isEmpty()) continue;
            if (canonicalTarget == canonicalAllowed || canonicalTarget.startsWith(canonicalAllowed + "/")) {
                *ok = true;
                return canonicalTarget;
            }
        }
        qDebug()<<"[TOOL_EXEC] 路径越界被拒绝 | rel="<<relativePath<<"|canonicalTarget="<<canonicalTarget;
        return QString();
    }

    *ok = true;
    return canonicalTarget;
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

QString ToolExecutor::writeFile(const QString &workspaceRoot, const QJsonObject &args,
                                 bool *ok, QString *displayLabel, QString *diffOutput)
{
    *ok = false;
    const QString relPath = args.value("path").toString();
    const QString content = args.value("content").toString();
    *displayLabel = QString("写入 %1").arg(relPath.isEmpty() ? QStringLiteral("(空路径)") : relPath);

    qDebug()<<"[TOOL_EXEC] writeFile | relPath="<<relPath<<"|内容长度="<<content.length();

    if (relPath.isEmpty()) {
        return QStringLiteral("错误：未提供 path 参数");
    }

    bool pathOk = false;
    const QString fullPath = resolveSafePath(workspaceRoot, relPath, &pathOk);
    if (!pathOk) {
        return QString("错误：路径 \"%1\" 不在工作区目录内，已拒绝访问").arg(relPath);
    }

    if (content.toUtf8().size() > kMaxWriteFileSize) {
        return QString("错误：写入内容过大（约 %1 KB），超过 1MB 上限").arg(content.toUtf8().size() / 1024);
    }

    // 如果文件已存在，读取旧内容用于 diff
    QString oldContent;
    bool isNewFile = false;
    bool oldFileTooLargeForDiff = false;
    QFileInfo fi(fullPath);
    if (fi.exists()) {
        if (fi.size() > kMaxReadFileSize) {
            // 旧文件本身就很大，不读入内存做diff了（generateDiff内部虽然也有规模保护，
            // 但没必要为了跳过diff还白白读一遍大文件）
            oldFileTooLargeForDiff = true;
        } else {
            QFile oldFile(fullPath);
            if (oldFile.open(QIODevice::ReadOnly)) {
                oldContent = QString::fromUtf8(oldFile.readAll());
                oldFile.close();
            }
        }
    } else {
        isNewFile = true;
        // 确保父目录存在
        QDir().mkpath(fi.absolutePath());
    }

    // 写入文件
    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString("错误：无法写入文件: %1").arg(relPath);
    }
    file.write(content.toUtf8());
    file.close();

    *ok = true;
    *displayLabel = QString("写入 %1 (%2 行)").arg(relPath).arg(content.count('\n') + 1);

    // 生成 diff 用于确认显示
    if (diffOutput) {
        if (isNewFile) {
            *diffOutput = QString("📄 创建新文件: %1\n").arg(relPath);
            const QStringList lines = content.split('\n');
            for (const QString &line : lines) {
                *diffOutput += "+ " + line + "\n";
            }
        } else if (oldFileTooLargeForDiff) {
            *diffOutput = QString("📄 %1\n（原文件较大，跳过读取生成diff，仅确认：即将用新内容整体覆盖该文件）").arg(relPath);
        } else {
            *diffOutput = generateDiff(oldContent, content, relPath);
        }
    }

    return QString("成功写入文件 %1（%2 行）").arg(relPath).arg(content.count('\n') + 1);
}

QString ToolExecutor::applyPatch(const QString &workspaceRoot, const QJsonObject &args,
                                  bool *ok, QString *displayLabel, QString *diffOutput)
{
    *ok = false;
    const QString relPath = args.value("path").toString();
    const QJsonArray patches = args.value("patches").toArray();
    *displayLabel = QString("修改 %1").arg(relPath.isEmpty() ? QStringLiteral("(空路径)") : relPath);

    qDebug()<<"[TOOL_EXEC] applyPatch | relPath="<<relPath<<"|patches数量="<<patches.size();

    if (relPath.isEmpty()) {
        return QStringLiteral("错误：未提供 path 参数");
    }
    if (patches.isEmpty()) {
        return QStringLiteral("错误：未提供 patches 参数");
    }
    if (patches.size() > kMaxPatchCount) {
        return QString("错误：patches 数量 %1 超过上限 %2").arg(patches.size()).arg(kMaxPatchCount);
    }

    bool pathOk = false;
    const QString fullPath = resolveSafePath(workspaceRoot, relPath, &pathOk);
    if (!pathOk) {
        return QString("错误：路径 \"%1\" 不在工作区目录内，已拒绝访问").arg(relPath);
    }

    // 读取原文件
    QFileInfo fi(fullPath);
    if (!fi.exists()) {
        return QString("错误：文件不存在: %1").arg(relPath);
    }
    if (fi.size() > kMaxReadFileSize) {
        return QString("错误：文件过大（约 %1 KB），无法进行 patch 操作").arg(fi.size() / 1024);
    }

    QFile readFile(fullPath);
    if (!readFile.open(QIODevice::ReadOnly)) {
        return QString("错误：无法打开文件: %1").arg(relPath);
    }
    const QByteArray fileData = readFile.readAll();
    readFile.close();

    if (looksBinary(fileData)) {
        return QString("错误：\"%1\" 看起来是二进制文件，无法进行 patch 操作").arg(relPath);
    }

    QString content = QString::fromUtf8(fileData);
    QStringList results;

    // 逐个应用 patch
    for (int i = 0; i < patches.size(); ++i) {
        const QJsonObject patch = patches[i].toObject();
        const QString search = patch["search"].toString();
        const QString replace = patch["replace"].toString();

        if (search.isEmpty()) {
            results << QString("patch[%1] 错误：search 不能为空").arg(i);
            continue;
        }

        int pos = content.indexOf(search);
        int matchLen = search.size();
        bool usedFuzzyMatch = false;

        if (pos == -1) {
            // 精确匹配失败，尝试忽略每行首尾空白的容错匹配——
            // AI生成的代码经常在缩进/行尾空白上跟文件本身有细微出入
            int fuzzyStart = -1, fuzzyLen = 0;
            if (findFuzzyLineMatch(content, search, &fuzzyStart, &fuzzyLen)) {
                pos = fuzzyStart;
                matchLen = fuzzyLen;
                usedFuzzyMatch = true;
            }
        }

        if (pos == -1) {
            results << QString("patch[%1] 错误：未找到匹配的文本（已尝试忽略空白差异的匹配，仍未找到）").arg(i);
            *ok = false;
            // 不继续执行，防止部分修改导致文件状态不一致
            return QString("apply_patch 失败：\n") + results.join('\n')
                   + QString("\npatch[%1]: 未在文件中找到匹配的内容:\n```\n%2\n```").arg(i).arg(search.left(200));
        }

        if (!usedFuzzyMatch) {
            // 只有精确匹配才需要额外查"是否唯一"；模糊匹配内部已经保证了唯一性
            const int secondPos = content.indexOf(search, pos + 1);
            if (secondPos != -1) {
                *ok = false;
                return QString("apply_patch 失败：\npatch[%1]: 找到多处匹配，请提供更多上下文以确保唯一匹配。"
                               "匹配文本:\n```\n%2\n```").arg(i).arg(search.left(200));
            }
        }

        // 执行替换
        content.replace(pos, matchLen, replace);
        results << QString(usedFuzzyMatch ? "patch[%1] 成功（使用了空白容错匹配）" : "patch[%1] 成功").arg(i);
    }

    // 写出修改后的内容
    QFile writeFile(fullPath);
    if (!writeFile.open(QIODevice::WriteOnly)) {
        return QString("错误：无法写入文件: %1").arg(relPath);
    }
    writeFile.write(content.toUtf8());
    writeFile.close();

    *ok = true;
    const int lineCount = content.count('\n') + 1;
    *displayLabel = QString("修改 %1 (%2 行，%3 个 patch)").arg(relPath).arg(lineCount).arg(patches.size());

    // 生成 diff
    if (diffOutput) {
        *diffOutput = generateDiff(QString::fromUtf8(fileData), content, relPath);
    }

    return QString("成功对 %1 应用了 %2 个 patch\n%3").arg(relPath).arg(patches.size()).arg(results.join('\n'));
}

QString ToolExecutor::generateDiff(const QString &originalContent, const QString &newContent,
                                    const QString &relPath)
{
    const QStringList oldLines = originalContent.split('\n');
    const QStringList newLines = newContent.split('\n');
    const int n = oldLines.size();
    const int m = newLines.size();

    // LCS 的时间/空间复杂度是 O(n*m)，行数一多这个 dp 表会迅速膨胀（比如两个几千行的文件
    // 就是几千万个int，几百MB内存 + 明显卡顿）。超过阈值就放弃精确diff，只给一个轻量摘要，
    // 避免为了生成一个"确认弹窗里看的diff"把界面卡死。
    constexpr qint64 kMaxDiffCells = 4'000'000; // 约等于两个2000行文件互相比较的量级
    if (static_cast<qint64>(n) * static_cast<qint64>(m) > kMaxDiffCells) {
        return QString("📄 %1\n"
                       "（文件较大：原文件 %2 行 → 新内容 %3 行，跳过逐行diff以避免卡顿，"
                       "请在确认前自行确认修改范围是否符合预期）")
            .arg(relPath).arg(n).arg(m);
    }

    // 标准LCS(最长公共子序列)动态规划，用来正确处理"插入/删除导致后续行整体错位"的情况。
    // 不用这个的话，简单按行号对齐比较，插入哪怕一行都会让后面所有行被误判为"整行修改"，
    // confirm弹窗里的diff就没法看了。
    //
    // dp[i][j] = oldLines前i行 和 newLines前j行 的最长公共子序列长度
    QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));
    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            if (oldLines[i - 1] == newLines[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else {
                dp[i][j] = qMax(dp[i - 1][j], dp[i][j - 1]);
            }
        }
    }

    // 从dp表回溯，得到每一行是"保留"、"删除"还是"新增"
    enum class LineOp { Keep, Delete, Insert };
    QVector<QPair<LineOp, QString>> ops;
    int i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && oldLines[i - 1] == newLines[j - 1]) {
            ops.prepend({ LineOp::Keep, oldLines[i - 1] });
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            ops.prepend({ LineOp::Insert, newLines[j - 1] });
            --j;
        } else {
            ops.prepend({ LineOp::Delete, oldLines[i - 1] });
            --i;
        }
    }

    // 渲染成人类可读的diff文本，未变化的行前后各保留2行上下文，减少噪音
    constexpr int kContextLines = 2;
    QStringList diff;
    diff << QString("📄 %1").arg(relPath);

    int idx = 0;
    while (idx < ops.size()) {
        if (ops[idx].first == LineOp::Keep) {
            // 跳过一大段没变化的内容时用省略号提示，避免整段未改动的代码也全部列出来
            int keepStart = idx;
            while (idx < ops.size() && ops[idx].first == LineOp::Keep) ++idx;
            int keepCount = idx - keepStart;

            bool nearChangeBefore = (keepStart == 0);
            bool nearChangeAfter = (idx == ops.size());
            int headShow = nearChangeBefore ? 0 : kContextLines;
            int tailShow = nearChangeAfter ? 0 : kContextLines;

            if (keepCount <= headShow + tailShow) {
                for (int k = keepStart; k < idx; ++k) {
                    diff << QString("    %1").arg(ops[k].second);
                }
            } else {
                for (int k = keepStart; k < keepStart + headShow; ++k) {
                    diff << QString("    %1").arg(ops[k].second);
                }
                diff << QString("    ... (%1 行未变化) ...").arg(keepCount - headShow - tailShow);
                for (int k = idx - tailShow; k < idx; ++k) {
                    diff << QString("    %1").arg(ops[k].second);
                }
            }
        } else if (ops[idx].first == LineOp::Delete) {
            diff << QString("  - %1").arg(ops[idx].second);
            ++idx;
        } else {
            diff << QString("  + %1").arg(ops[idx].second);
            ++idx;
        }
    }

    return diff.join('\n');
}

QString ToolExecutor::previewDiff(const QString &workspaceRoot, const QString &toolName,
                                   const QJsonObject &arguments, bool *ok, QString *displayLabel)
{
    *ok = false;
    qDebug()<<"[TOOL_EXEC] previewDiff | toolName="<<toolName;

    if (toolName == QLatin1String("write_file")) {
        const QString relPath = arguments.value("path").toString();
        const QString content = arguments.value("content").toString();
        *displayLabel = QString("预览写入 %1").arg(relPath.isEmpty() ? QStringLiteral("(空路径)") : relPath);

        if (relPath.isEmpty()) {
            return QStringLiteral("错误：未提供 path 参数");
        }

        bool pathOk = false;
        const QString fullPath = resolveSafePath(workspaceRoot, relPath, &pathOk);
        if (!pathOk) {
            return QString("错误：路径 \"%1\" 不在工作区目录内，已拒绝访问").arg(relPath);
        }

        QString oldContent;
        QFileInfo fi(fullPath);
        if (fi.exists()) {
            if (fi.size() > kMaxReadFileSize) {
                *ok = true;
                *displayLabel = QString("预览修改 %1").arg(relPath);
                return QString("📄 %1\n（原文件较大，跳过读取生成diff，仅确认：即将用新内容整体覆盖该文件）").arg(relPath);
            }
            QFile oldFile(fullPath);
            if (oldFile.open(QIODevice::ReadOnly)) {
                oldContent = QString::fromUtf8(oldFile.readAll());
                oldFile.close();
            }
            *ok = true;
            *displayLabel = QString("预览修改 %1").arg(relPath);
            return generateDiff(oldContent, content, relPath);
        } else {
            *ok = true;
            *displayLabel = QString("预览创建 %1").arg(relPath);
            QStringList diff;
            diff << QString("📄 创建新文件: %1").arg(relPath);
            const QStringList lines = content.split('\n');
            for (const QString &line : lines) {
                diff << "+ " + line;
            }
            return diff.join('\n');
        }
    }

    if (toolName == QLatin1String("apply_patch")) {
        const QString relPath = arguments.value("path").toString();
        const QJsonArray patches = arguments.value("patches").toArray();
        *displayLabel = QString("预览修改 %1").arg(relPath.isEmpty() ? QStringLiteral("(空路径)") : relPath);

        if (relPath.isEmpty()) {
            return QStringLiteral("错误：未提供 path 参数");
        }

        bool pathOk = false;
        const QString fullPath = resolveSafePath(workspaceRoot, relPath, &pathOk);
        if (!pathOk) {
            return QString("错误：路径 \"%1\" 不在工作区目录内，已拒绝访问").arg(relPath);
        }

        QFileInfo fi(fullPath);
        if (!fi.exists()) {
            return QString("错误：文件不存在: %1").arg(relPath);
        }

        QFile readFile(fullPath);
        if (!readFile.open(QIODevice::ReadOnly)) {
            return QString("错误：无法打开文件: %1").arg(relPath);
        }
        const QByteArray fileData = readFile.readAll();
        readFile.close();

        if (looksBinary(fileData)) {
            return QString("错误：二进制文件，无法预览");
        }

        QString content = QString::fromUtf8(fileData);
        for (int i = 0; i < patches.size(); ++i) {
            const QJsonObject patch = patches[i].toObject();
            const QString search = patch["search"].toString();
            const QString replace = patch["replace"].toString();
            if (search.isEmpty()) continue;

            const int pos = content.indexOf(search);
            if (pos != -1) {
                content.replace(pos, search.size(), replace);
            }
        }

        *ok = true;
        *displayLabel = QString("预览修改 %1 (%2 个 patch)").arg(relPath).arg(patches.size());
        return generateDiff(QString::fromUtf8(fileData), content, relPath);
    }

    *ok = false;
    *displayLabel = QString("不支持预览: %1").arg(toolName);
    return QString();
}

bool ToolExecutor::isBlacklistedCommand(const QString &command, QString *reason)
{
    const QString cmd = command.trimmed().toLower();

    // 按空格分割取第一个词作为基本命令
    const QString base = cmd.section(' ', 0, 0);

    // 跨平台高危命令
    static const QStringList kBlacklist = {
        // 文件/磁盘破坏
        "format", "mkfs", "dd", "fdisk", "parted", "mke2fs",
        // 权限变更
        "chmod", "chown", "chattr",
        // 系统操作
        "shutdown", "reboot", "poweroff", "halt", "init",
        // 特别危险的 Windows 命令
        "del", "rmdir", "rd",
        // 包管理器全局操作（防止 AI 擅自安装/卸载全局包）
        "sudo", "doas", "pkexec",
    };

    // rm 只拦截带 -rf / -r / -f 破坏性标志的情况
    if (base == "rm" || base == "rmdir") {
        if (cmd.contains(" -rf") || cmd.contains(" -fr")
            || cmd.contains(" -r ") || cmd.contains(" -f ")
            || cmd.contains(" --recursive") || cmd.contains(" --force")) {
            if (reason) *reason = QString("禁止递归/强制删除文件: %1").arg(command);
            return true;
        }
        // 允许 rm 单个文件
        return false;
    }

    // 精确匹配其它黑名单
    for (const QString &banned : kBlacklist) {
        if (base == banned) {
            if (reason) *reason = QString("命令 \"%1\" 被禁止执行（高危操作）").arg(banned);
            return true;
        }
    }

    // 阻止管道到危险命令
    if (cmd.contains("| sudo") || cmd.contains("| doas")) {
        if (reason) *reason = "禁止通过管道提权执行命令";
        return true;
    }

    return false;
}

QString ToolExecutor::runCommand(const QString &workspaceRoot, const QJsonObject &args,
                                  bool *ok, QString *displayLabel)
{
    *ok = false;
    const QString command = args.value("command").toString().trimmed();
    *displayLabel = QString("执行命令: %1").arg(command.left(60));

    if (command.isEmpty()) {
        return "错误：未提供 command 参数";
    }

    // 黑名单检查
    QString blockReason;
    if (isBlacklistedCommand(command, &blockReason)) {
        return QString("错误：%1").arg(blockReason);
    }

    QProcess process;
    process.setWorkingDirectory(workspaceRoot);
    process.setProcessChannelMode(QProcess::SeparateChannels);

    // Windows 下用 cmd /c，其它平台用 sh -c
#ifdef Q_OS_WIN
    process.start("cmd.exe", QStringList{ "/c", command });
#else
    process.start("/bin/sh", QStringList{ "-c", command });
#endif

    // 等待最多 30 秒
    constexpr int kCommandTimeoutMs = 30000;
    if (!process.waitForStarted(5000)) {
        return QString("错误：无法启动命令 \"%1\": %2")
            .arg(command.left(100), process.errorString());
    }

    if (!process.waitForFinished(kCommandTimeoutMs)) {
        process.kill();
        *ok = false;
        *displayLabel = QString("命令超时: %1").arg(command.left(40));
        const QByteArray partialOut = process.readAllStandardOutput();
        return QString("错误：命令执行超时（30 秒），已自动终止。\n命令: %1\n\n部分输出:\n%2")
            .arg(command.left(200), QString::fromUtf8(partialOut));
    }

    const QByteArray output = process.readAllStandardOutput();
    const QByteArray errOutput = process.readAllStandardError();
    const int exitCode = process.exitCode();

    QString result;
    if (exitCode == 0) {
        *ok = true;
        result = QString("命令执行成功（退出码: 0）\n\n");
    } else {
        *ok = false;
        result = QString("命令执行失败（退出码: %1）\n\n").arg(exitCode);
    }

    if (!output.isEmpty()) {
        result += "--- stdout ---\n" + QString::fromUtf8(output) + "\n";
    }
    if (!errOutput.isEmpty()) {
        result += "--- stderr ---\n" + QString::fromUtf8(errOutput) + "\n";
    }

    // 限制返回长度
    constexpr int kMaxOutputLen = 16000;
    if (result.size() > kMaxOutputLen) {
        result = result.left(kMaxOutputLen)
                 + QString("\n\n... (输出过长，已截断至 %1 字符)").arg(kMaxOutputLen);
    }

    *displayLabel = QString("执行: %1 (退出码 %2)")
        .arg(command.left(40)).arg(exitCode);
    return result;
}

QString ToolExecutor::execute(const QString &workspaceRoot, const QString &toolName,
                              const QJsonObject &arguments, bool *ok, QString *displayLabel,
                              QString *diffOutput)
{
    qDebug()<<"[TOOL_EXEC] execute | toolName="<<toolName;

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
    if (toolName == QLatin1String("write_file")) {
        return writeFile(workspaceRoot, arguments, ok, displayLabel, diffOutput);
    }
    if (toolName == QLatin1String("apply_patch")) {
        return applyPatch(workspaceRoot, arguments, ok, displayLabel, diffOutput);
    }
    if (toolName == QLatin1String("run_command")) {
        return runCommand(workspaceRoot, arguments, ok, displayLabel);
    }

    *ok = false;
    *displayLabel = QString("未知工具: %1").arg(toolName);
    return QString("错误：不支持的工具名称: %1").arg(toolName);
}
