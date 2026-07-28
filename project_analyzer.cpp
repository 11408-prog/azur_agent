#include "project_analyzer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDebug>
#include <QCoreApplication>
#include <QDirIterator>
#include <utility>

// ==================== 默认跳过目录 ====================
QStringList ProjectAnalyzer::defaultSkipDirs()
{
    return {
        QStringLiteral("build"),
        QStringLiteral("build_fresh"),
        QStringLiteral(".git"),
        QStringLiteral(".qtcreator"),
        QStringLiteral(".claude"),
        QStringLiteral(".azur"),
        QStringLiteral("lib"),
        QStringLiteral("bin"),
        QStringLiteral("obj"),
        QStringLiteral("node_modules"),
        QStringLiteral("__pycache__"),
        QStringLiteral(".vscode"),
        QStringLiteral(".idea"),
        QStringLiteral("cmake-build-debug"),
        QStringLiteral("cmake-build-release"),
        QStringLiteral("x64"),
        QStringLiteral("Release"),
        QStringLiteral("Debug"),
        QStringLiteral("daily_log"),
        QStringLiteral("avatar"),
    };
}

// ==================== 文件最后修改时间 ====================
QString ProjectAnalyzer::fileLastModified(const QString &filePath)
{
    QFileInfo fi(filePath);
    if (!fi.exists()) return {};
    return fi.lastModified().toString(Qt::ISODate);
}

// ==================== 变更检测 ====================
bool ProjectAnalyzer::needsRebuild(const QString &workspaceRoot)
{
    const QJsonObject cached = loadIndex(workspaceRoot);
    if (cached.isEmpty()) return true;

    const QString lastAnalyzed = cached["analyzedAt"].toString();
    if (lastAnalyzed.isEmpty()) return true;

    // 读取之前的文件修改时间快照
    const QJsonObject fileTimestamps = cached["fileTimestamps"].toObject();
    if (fileTimestamps.isEmpty()) return true;

    // 检查各个源文件是否被改过
    const QJsonArray files = cached["files"].toArray();
    for (const QJsonValue &v : files) {
        const QString relPath = v.toObject()["path"].toString();
        if (relPath.isEmpty()) continue;
        const QString fullPath = QDir(workspaceRoot).filePath(relPath);
        const QString currentMtime = fileLastModified(fullPath);
        const QString cachedMtime = fileTimestamps[relPath].toString();
        if (currentMtime != cachedMtime) {
            qDebug() << "[ANALYZER] 文件已变更，需要重建索引:" << relPath;
            return true;
        }
    }

    return false;
}

// ==================== 主入口：构建索引 ====================
QJsonObject ProjectAnalyzer::buildIndex(const QString &workspaceRoot)
{
    qDebug() << "[ANALYZER] 开始构建项目索引 | workspaceRoot=" << workspaceRoot;

    QJsonObject index;

    QDir rootDir(workspaceRoot);
    if (!rootDir.exists()) {
        qWarning() << "[ANALYZER] 工作区目录不存在:" << workspaceRoot;
        index["error"] = "工作区目录不存在";
        return index;
    }

    const QString projectName = rootDir.dirName();

    // ---- 递归扫描文件 ----
    QList<FileEntry> files;
    scanDirectory(workspaceRoot, workspaceRoot, files, defaultSkipDirs());

    // ---- 判断语言和框架 ----
    QStringList extensions;
    for (const auto &f : qAsConst(files)) {
        if (!extensions.contains(f.extension)) {
            extensions.append(f.extension);
        }
    }
    const QString language = detectLanguage(extensions);
    const QString framework = detectFramework(files, workspaceRoot);

    // ---- 提取顶层类和函数 ----
    QList<ClassEntry> classes;
    QList<FunctionEntry> functions;
    extractClassesAndFunctions(files, classes, functions);

    // ---- 统计 ----
    int totalLines = 0;
    for (const auto &f : std::as_const(files)) {
        totalLines += f.lineCount;
    }

    // ---- 记录文件修改时间戳 ----
    QJsonObject fileTimestamps;
    for (const auto &f : std::as_const(files)) {
        const QString fullPath = QDir(workspaceRoot).filePath(f.relPath);
        fileTimestamps[f.relPath] = fileLastModified(fullPath);
    }

    // ---- 序列化 ----
    QJsonArray filesArr;
    for (const auto &f : std::as_const(files)) {
        QJsonObject fo;
        fo["path"] = f.relPath;
        fo["extension"] = f.extension;
        fo["sizeBytes"] = static_cast<qint64>(f.sizeBytes);
        fo["lineCount"] = f.lineCount;

        QJsonArray incArr;
        for (const QString &inc : f.includes) incArr.append(inc);
        fo["includes"] = incArr;

        QJsonArray clsArr;
        for (const QString &c : f.classes) clsArr.append(c);
        fo["classes"] = clsArr;

        QJsonArray funcArr;
        for (const QString &fn : f.functions) funcArr.append(fn);
        fo["functions"] = funcArr;

        filesArr.append(fo);
    }

    QJsonArray classesArr;
    for (const auto &c : classes) {
        QJsonObject co;
        co["name"] = c.name;
        co["filePath"] = c.filePath;
        co["lineNumber"] = c.lineNumber;
        QJsonArray bases;
        for (const QString &b : c.baseClasses) bases.append(b);
        co["baseClasses"] = bases;
        QJsonArray methods;
        for (const QString &m : c.methods) methods.append(m);
        co["methods"] = methods;
        classesArr.append(co);
    }

    QJsonArray funcsArr;
    for (const auto &fn : functions) {
        QJsonObject fo;
        fo["name"] = fn.name;
        fo["returnType"] = fn.returnType;
        fo["filePath"] = fn.filePath;
        fo["lineNumber"] = fn.lineNumber;
        funcsArr.append(fo);
    }

    index["projectName"] = projectName;
    index["analyzedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    index["language"] = language;
    index["framework"] = framework;
    index["totalFiles"] = files.size();
    index["totalLines"] = totalLines;
    index["files"] = filesArr;
    index["classes"] = classesArr;
    index["functions"] = funcsArr;
    index["fileTimestamps"] = fileTimestamps;

    qDebug() << "[ANALYZER] 索引构建完成 | 文件数=" << files.size()
             << "| 代码行=" << totalLines
             << "| 类=" << classes.size()
             << "| 函数=" << functions.size()
             << "| 语言=" << language
             << "| 框架=" << framework;

    return index;
}

// ==================== 递归扫描目录 ====================
void ProjectAnalyzer::scanDirectory(const QString &dirPath, const QString &workspaceRoot,
                                     QList<FileEntry> &files,
                                     const QStringList &skipDirs)
{
    QDir dir(dirPath);
    if (!dir.exists()) return;

    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name);

    static int processCounter = 0;
    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            // 检查是否是要跳过的目录
            bool skip = false;
            for (const QString &s : skipDirs) {
                if (entry.fileName() == s) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
            // 递归
            scanDirectory(entry.absoluteFilePath(), workspaceRoot, files, skipDirs);
        } else if (entry.isFile()) {
            // 跳过隐藏文件（以 . 开头）
            if (entry.fileName().startsWith('.')) continue;

            // 跳过明显的大文件（> 5MB）和非文本格式
            const QString ext = entry.suffix().toLower();
            if (ext == "exe" || ext == "dll" || ext == "lib" || ext == "obj"
                || ext == "pdb" || ext == "png" || ext == "jpg" || ext == "jpeg"
                || ext == "gif" || ext == "bmp" || ext == "ico" || ext == "svg"
                || ext == "ttf" || ext == "otf" || ext == "woff" || ext == "woff2"
                || ext == "mp3" || ext == "mp4" || ext == "avi" || ext == "mkv"
                || ext == "zip" || ext == "rar" || ext == "7z" || ext == "tar"
                || ext == "gz" || ext == "pdf" || ext == "doc" || ext == "docx"
                || ext == "xls" || ext == "xlsx" || ext == "o" || ext == "a") {
                continue;
            }

            // 跳过空文件
            if (entry.size() == 0) continue;

            FileEntry fe;
            fe.relPath = QDir(workspaceRoot).relativeFilePath(entry.absoluteFilePath());
            fe.extension = ext.isEmpty() ? QStringLiteral("(无后缀)") : ("." + ext);
            fe.sizeBytes = entry.size();

            // 粗略估算行数：对于小文件直接读入计数，大文件用大小估算
            if (entry.size() > 1024 * 1024) {
                // 大于 1MB 的文本文件，用大小估算行数（假设平均每行 60 字节）
                fe.lineCount = static_cast<int>(entry.size() / 60);
            } else {
                QFile f(entry.absoluteFilePath());
                if (f.open(QIODevice::ReadOnly)) {
                    const QByteArray data = f.readAll();
                    f.close();
                    fe.lineCount = data.count('\n') + 1;
                    // 检查是否是二进制
                    for (int i = 0; i < qMin(data.size(), 1024); ++i) {
                        if (data.at(i) == '\0') {
                            fe.lineCount = 0; // 标记为二进制，不做分析
                            break;
                        }
                    }
                }
            }

            if (fe.lineCount == 0) continue; // 跳过二进制文件

            // 周期性处理 UI 事件，防止大项目扫描时界面冻结
            ++processCounter;
            if (processCounter % 20 == 0) {
                QCoreApplication::processEvents();
            }

            // 根据扩展名做语言分析
            const QString extLower = entry.suffix().toLower();
            if (extLower == "cpp" || extLower == "cc" || extLower == "cxx") {
                analyzeCppFile(entry.absoluteFilePath(), fe.relPath, fe);
            } else if (extLower == "h" || extLower == "hpp" || extLower == "hxx") {
                analyzeHeaderFile(entry.absoluteFilePath(), fe.relPath, fe);
            } else if (extLower == "py") {
                analyzePythonFile(entry.absoluteFilePath(), fe.relPath, fe);
            }
            // 其他文件类型暂时只统计行数，不做深入分析

            files.append(fe);
        }
    }
}

// ==================== C++ 源文件分析 ====================
void ProjectAnalyzer::analyzeCppFile(const QString &filePath, const QString &relPath,
                                      FileEntry &entry)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    const QByteArray rawData = file.readAll();
    file.close();

    if (rawData.size() > 300 * 1024) {
        // 大文件只扫开头和结尾的关键结构，不全文解析
        const QByteArray head = rawData.left(100 * 1024);
        const QByteArray tail = rawData.right(20 * 1024);
        const QByteArray combined = head + "\n" + tail;
        QString text = QString::fromUtf8(combined);
        analyzeCppText(text, entry);
        return;
    }

    const QString text = QString::fromUtf8(rawData);
    analyzeCppText(text, entry);
}

// 辅助函数：分析 C++ 文本内容
static void analyzeCppTextImpl(const QString &text, QStringList &includes,
                                QStringList &classes, QStringList &functions)
{
    // ---- includes ----
    static const QRegularExpression incRe(
        QStringLiteral(R"(#include\s+[<"]([^>"]+)[>"])"));
    auto incIt = incRe.globalMatch(text);
    while (incIt.hasNext()) {
        const auto match = incIt.next();
        includes << match.captured(1);
    }

    // ---- class / struct ----
    // 匹配 class Xxx / struct Xxx，以及可能的继承
    static const QRegularExpression classRe(
        QStringLiteral(R"(\b(?:class|struct)\s+(\w+)\s*(?::\s*(?:public|private|protected)\s+(\w+))?)"));
    auto classIt = classRe.globalMatch(text);
    while (classIt.hasNext()) {
        const auto match = classIt.next();
        QString cls = match.captured(1);
        // 过滤掉明显不是类名的（全大写缩写/Qt宏之类）
        if (cls.startsWith("Q_") || cls == "public" || cls == "private" || cls == "protected")
            continue;
        classes << cls;
    }

    // ---- 函数声明（粗略） ----
    // 匹配: 返回类型 函数名(参数) { 或 ;
    // 不完美但够用：匹配 "word word(" 或 "word::word(" 模式，且后面跟着 { 或 ;
    static const QRegularExpression funcRe(
        QStringLiteral(R"(\b([a-zA-Z_]\w*(?:\s*[*&]?)?)\s+([a-zA-Z_]\w*)\s*\(([^)]*)\)\s*(?:const|override|final|noexcept)?\s*(\{|;))"));
    auto funcIt = funcRe.globalMatch(text);
    while (funcIt.hasNext()) {
        const auto match = funcIt.next();
        QString returnType = match.captured(1).trimmed();
        QString funcName = match.captured(2);

        // 过滤掉明显不是函数的
        if (funcName == "if" || funcName == "for" || funcName == "while"
            || funcName == "switch" || funcName == "catch" || funcName == "return"
            || funcName == "delete" || funcName == "new" || funcName == "emit"
            || funcName.startsWith("Q_"))
            continue;

        // 过滤掉过于简单的返回类型（纯关键字）
        if (returnType == "if" || returnType == "for" || returnType == "while"
            || returnType == "switch" || returnType == "catch")
            continue;

        // 过滤掉匿名函数 lambda
        if (funcName == "operator") continue;

        // 去重
        if (!functions.contains(funcName)) {
            functions << funcName;
        }
    }

    // ---- Q_OBJECT 宏中的信号/槽 ----
    static const QRegularExpression slotRe(
        QStringLiteral(R"(\b(?:void|int|bool|QString|QWidget|QObject)\s+(\w+)\s*\([^)]*\))"));

    // 去重
    functions.removeDuplicates();
    classes.removeDuplicates();
}

void ProjectAnalyzer::analyzeCppText(const QString &text, FileEntry &entry)
{
    analyzeCppTextImpl(text, entry.includes, entry.classes, entry.functions);
}

// ==================== 头文件分析 ====================
void ProjectAnalyzer::analyzeHeaderFile(const QString &filePath, const QString &relPath,
                                         FileEntry &entry)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    const QByteArray rawData = file.readAll();
    file.close();

    if (rawData.size() > 300 * 1024) return;

    const QString text = QString::fromUtf8(rawData);
    analyzeCppText(text, entry);
}

// ==================== Python 文件分析 ====================
void ProjectAnalyzer::analyzePythonFile(const QString &filePath, const QString &relPath,
                                         FileEntry &entry)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    const QByteArray rawData = file.readAll();
    file.close();

    if (rawData.size() > 300 * 1024) return;

    const QString text = QString::fromUtf8(rawData);
    const QStringList lines = text.split('\n');

    // ---- import ----
    static const QRegularExpression importRe(
        QStringLiteral(R"(^(?:import|from)\s+(\S+))"));
    for (const QString &line : lines) {
        auto match = importRe.match(line);
        if (match.hasMatch()) {
            entry.includes << match.captured(1);
        }
    }

    // ---- class ----
    static const QRegularExpression classRe(
        QStringLiteral(R"(^\s*class\s+(\w+)\s*(?:\(([^)]*)\))?\s*:)"));
    for (const QString &line : lines) {
        auto match = classRe.match(line);
        if (match.hasMatch()) {
            entry.classes << match.captured(1);
        }
    }

    // ---- function ----
    static const QRegularExpression funcRe(
        QStringLiteral(R"(^\s*def\s+(\w+)\s*\()"));
    for (const QString &line : lines) {
        auto match = funcRe.match(line);
        if (match.hasMatch()) {
            entry.functions << match.captured(1);
        }
    }

    entry.includes.removeDuplicates();
    entry.classes.removeDuplicates();
    entry.functions.removeDuplicates();
}

// ==================== 语言检测 ====================
QString ProjectAnalyzer::detectLanguage(const QStringList &extensions)
{
    if (extensions.contains(".cpp") || extensions.contains(".h")
        || extensions.contains(".hpp") || extensions.contains(".cc")) {
        return "C++";
    }
    if (extensions.contains(".py")) {
        return "Python";
    }
    if (extensions.contains(".java")) {
        return "Java";
    }
    if (extensions.contains(".rs")) {
        return "Rust";
    }
    if (extensions.contains(".go")) {
        return "Go";
    }
    if (extensions.contains(".js") || extensions.contains(".ts")) {
        return "JavaScript/TypeScript";
    }
    if (extensions.contains(".cs")) {
        return "C#";
    }
    return "未知";
}

// ==================== 框架检测 ====================
QString ProjectAnalyzer::detectFramework(const QList<FileEntry> &files,
                                          const QString &workspaceRoot)
{
    // 检查 CMakeLists.txt
    if (QFileInfo::exists(QDir(workspaceRoot).filePath("CMakeLists.txt"))) {
        QFile cmake(QDir(workspaceRoot).filePath("CMakeLists.txt"));
        if (cmake.open(QIODevice::ReadOnly)) {
            const QString content = QString::fromUtf8(cmake.readAll());
            cmake.close();

            if (content.contains("find_package(Qt", Qt::CaseInsensitive)
                || content.contains("Qt::", Qt::CaseInsensitive)) {
                // 提取 Qt 版本
                static const QRegularExpression qtVerRe(
                    QStringLiteral(R"(find_package\(Qt(\d+))"));
                auto match = qtVerRe.match(content);
                if (match.hasMatch()) {
                    return QString("Qt%1").arg(match.captured(1));
                }
                return "Qt";
            }
        }
    }

    // 检查 includes 中的框架痕迹
    for (const auto &f : files) {
        for (const QString &inc : f.includes) {
            if (inc.startsWith("Q") && (inc.contains("Widget") || inc.contains("Core")
                || inc.contains("Network") || inc.contains("Gui"))) {
                return "Qt";
            }
        }
    }

    // 检查 package.json / Cargo.toml 等
    if (QFileInfo::exists(QDir(workspaceRoot).filePath("package.json"))) {
        return "Node.js";
    }
    if (QFileInfo::exists(QDir(workspaceRoot).filePath("Cargo.toml"))) {
        return "Rust/Cargo";
    }
    if (QFileInfo::exists(QDir(workspaceRoot).filePath("go.mod"))) {
        return "Go";
    }

    return "未知";
}

// ==================== 提取顶层类和函数 ====================
void ProjectAnalyzer::extractClassesAndFunctions(const QList<FileEntry> &files,
                                                  QList<ClassEntry> &classes,
                                                  QList<FunctionEntry> &functions)
{
    // 为每个文件再读一遍来提取位置信息（之前只提取了名字列表，没有行号）
    for (const auto &f : files) {
        if (f.classes.isEmpty() && f.functions.isEmpty()) continue;

        // FileEntry 使用的是 relPath，不需要判绝对路径
        // 这里直接作为文件路径标记使用
        const QString &sourcePath = f.relPath;

        // 从 includes 等路径信息我们已经有了 relPath
        // 但我们实际上在 scanDirectory 时已经把文件的绝对路径传给了 analyze 函数
        // 不过 analyze 函数里我们用 filePath 参数读取了内容，但没有保存行号
        // 这里 simple approach: 重新读取文件来获取行号
        // 实际上只在需要精确行号时才这么做，这里为了简洁先省略行号，用文件名代替
        // 更精确的做法在后面 generateSummary 里只展示文件名和类名，行号不是必须的

        for (const QString &clsName : f.classes) {
            ClassEntry ce;
            ce.name = clsName;
            ce.filePath = f.relPath;
            // 行号需要重新扫描文件，这里先设为 0，由 generateSummary 处理
            classes.append(ce);
        }

        for (const QString &fnName : f.functions) {
            FunctionEntry fe;
            fe.name = fnName;
            fe.filePath = f.relPath;
            functions.append(fe);
        }
    }
}

// ==================== 持久化：保存 / 加载 ====================
bool ProjectAnalyzer::saveIndex(const QString &workspaceRoot, const QJsonObject &index)
{
    const QString dirPath = workspaceRoot + "/.azur";
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QFile file(dir.filePath("project_index.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[ANALYZER] 无法写入 project_index.json:" << dir.filePath("project_index.json");
        return false;
    }

    file.write(QJsonDocument(index).toJson(QJsonDocument::Indented));
    file.close();
    qDebug() << "[ANALYZER] 索引已保存:" << dir.filePath("project_index.json");
    return true;
}

QJsonObject ProjectAnalyzer::loadIndex(const QString &workspaceRoot)
{
    QFile file(workspaceRoot + "/.azur/project_index.json");
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull() || !doc.isObject()) {
        return QJsonObject();
    }

    return doc.object();
}

// ==================== 摘要生成 ====================
QString ProjectAnalyzer::generateSummary(const QJsonObject &index,
                                          int maxClasses, int maxFiles)
{
    if (index.isEmpty() || index.contains("error")) {
        return QStringLiteral("（项目索引不可用）");
    }

    const QString projectName = index["projectName"].toString();
    const QString language = index["language"].toString();
    const QString framework = index["framework"].toString();
    const int totalFiles = index["totalFiles"].toInt();
    const int totalLines = index["totalLines"].toInt();
    const QJsonArray classes = index["classes"].toArray();
    const QJsonArray functions = index["functions"].toArray();
    const QJsonArray files = index["files"].toArray();

    QStringList lines;
    lines << "## 📋 项目索引";
    lines << QString();

    // ---- 概要 ----
    QString meta = QString("**语言:** %1").arg(language);
    if (framework != "未知") {
        meta += QString(" | **框架:** %1").arg(framework);
    }
    meta += QString(" | **文件数:** %1").arg(totalFiles);
    meta += QString(" | **代码行:** ~%1").arg(totalLines);
    lines << meta;
    lines << QString();

    // ---- 类列表 ----
    if (!classes.isEmpty()) {
        const int showCount = qMin(classes.size(), maxClasses);
        lines << QString("### 类 (%1%2)")
                     .arg(showCount)
                     .arg(classes.size() > maxClasses
                              ? QString(", 共 %1").arg(classes.size()) : "");
        for (int i = 0; i < showCount; ++i) {
            const QJsonObject cls = classes[i].toObject();
            const QString name = cls["name"].toString();
            const QString filePath = cls["filePath"].toString();
            const QJsonArray bases = cls["baseClasses"].toArray();
            const QJsonArray methods = cls["methods"].toArray();

            QString line = QString("- `%1`").arg(name);
            if (!bases.isEmpty()) {
                QStringList baseList;
                for (const auto &b : bases) baseList << b.toString();
                line += QString(" → %1").arg(baseList.join(", "));
            }
            line += QString(" (%1)").arg(filePath);
            if (!methods.isEmpty()) {
                line += QString(" — %1 个方法").arg(methods.size());
            }
            lines << line;
        }
        if (classes.size() > maxClasses) {
            lines << QString("  ... 还有 %1 个类未显示").arg(classes.size() - maxClasses);
        }
        lines << QString();
    }

    // ---- 关键文件（按代码行数排序，显示最大的几个） ----
    {
        // 转成 list 排序
        QList<QPair<int, QJsonObject>> sortedFiles;
        for (const QJsonValue &v : files) {
            const QJsonObject f = v.toObject();
            sortedFiles.append({f["lineCount"].toInt(), f});
        }
        std::sort(sortedFiles.begin(), sortedFiles.end(),
                   [](const QPair<int, QJsonObject> &a, const QPair<int, QJsonObject> &b) {
                       return a.first > b.first;
                   });

        const int showCount = qMin(sortedFiles.size(), maxFiles);
        lines << QString("### 关键文件 (%1)")
                     .arg(sortedFiles.size() > maxFiles
                              ? QString("%1/%2").arg(showCount).arg(sortedFiles.size())
                              : QString::number(showCount));
        for (int i = 0; i < showCount; ++i) {
            const QJsonObject &f = sortedFiles[i].second;
            const QString path = f["path"].toString();
            const int lc = f["lineCount"].toInt();
            const QJsonArray cls = f["classes"].toArray();
            const QJsonArray funcs = f["functions"].toArray();

            QString line = QString("- `%1` (%2 行)").arg(path).arg(lc);
            if (!cls.isEmpty()) {
                QStringList names;
                for (const auto &c : cls) names << c.toString();
                line += QString(" 类: %1").arg(names.join(", "));
            }
            lines << line;
        }
        if (sortedFiles.size() > maxFiles) {
            lines << QString("  ... 还有 %1 个文件未显示").arg(sortedFiles.size() - maxFiles);
        }
        lines << QString();
    }

    return lines.join('\n');
}
