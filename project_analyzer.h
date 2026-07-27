#ifndef PROJECT_ANALYZER_H
#define PROJECT_ANALYZER_H

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QList>

// 项目索引分析器
//
// 作用：扫描工作区目录，提取代码结构信息，生成结构化的项目索引。
// AI 在开始工作前通过索引快速了解项目全貌，而不需要从头逐个文件去读。
//
// 用法：
//   QJsonObject idx = ProjectAnalyzer::buildIndex(workspacePath);
//   ProjectAnalyzer::saveIndex(workspacePath, idx);
//   QString summary = ProjectAnalyzer::generateSummary(idx);
//
// 索引会缓存到 .azur/project_index.json，只有源文件发生变化时才重建。
class ProjectAnalyzer
{
public:
    // ---- 构建索引 ----
    // 扫描工作区目录，返回完整的索引 JSON 对象。
    // 跳过 build/ .git/ lib/ 等无关目录。
    static QJsonObject buildIndex(const QString &workspaceRoot);

    // ---- 持久化 ----
    static bool saveIndex(const QString &workspaceRoot, const QJsonObject &index);
    static QJsonObject loadIndex(const QString &workspaceRoot);

    // ---- 变更检测 ----
    // 比较源文件的最新修改时间与上次索引的缓存时间，
    // 如果没有任何文件发生变更则返回 false（可以复用缓存索引）。
    static bool needsRebuild(const QString &workspaceRoot);

    // ---- 摘要生成 ----
    // 将完整的索引 JSON 压缩成人类可读的短文本，适合放入 system prompt。
    // maxClasses / maxFiles 控制截断数量，避免摘要太长浪费 token。
    static QString generateSummary(const QJsonObject &index,
                                    int maxClasses = 20,
                                    int maxFiles = 30);

private:
    // ---- 内部扫描 ----
    struct FileEntry {
        QString relPath;
        QString extension;
        qint64 sizeBytes = 0;
        int lineCount = 0;
        QStringList includes;
        QStringList classes;
        QStringList functions;
    };

    struct ClassEntry {
        QString name;
        QString filePath;
        int lineNumber = 0;
        QStringList baseClasses;
        QStringList methods;
    };

    struct FunctionEntry {
        QString name;
        QString returnType;
        QString filePath;
        int lineNumber = 0;
    };

    static void scanDirectory(const QString &dirPath, const QString &workspaceRoot,
                               QList<FileEntry> &files,
                               const QStringList &skipDirs);

    static void analyzeCppFile(const QString &filePath, const QString &relPath,
                                FileEntry &entry);
    static void analyzePythonFile(const QString &filePath, const QString &relPath,
                                   FileEntry &entry);
    static void analyzeHeaderFile(const QString &filePath, const QString &relPath,
                                   FileEntry &entry);
    static void analyzeCppText(const QString &text, FileEntry &entry);

    static QString detectFramework(const QList<FileEntry> &files,
                                    const QString &workspaceRoot);
    static QString detectLanguage(const QStringList &extensions);
    static QStringList defaultSkipDirs();

    // 从 FileEntry 列表提取类和函数到顶层列表
    static void extractClassesAndFunctions(const QList<FileEntry> &files,
                                            QList<ClassEntry> &classes,
                                            QList<FunctionEntry> &functions);

    // 获取某个文件的最后修改时间戳（ISO 格式）
    static QString fileLastModified(const QString &filePath);
};

#endif // PROJECT_ANALYZER_H
