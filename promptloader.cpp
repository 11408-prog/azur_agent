#include "promptloader.h"

#include <QFile>
#include <QDebug>

QString PromptLoader::loadFile(const QString &filename)
{
    QFile fsFile(QString(RESOURCES_DIR) + "/" + filename);
    if (fsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(fsFile.readAll()).trimmed();
        return content;
    }
    QFile resFile(":/prompts/" + filename);
    if (resFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(resFile.readAll()).trimmed();
        return content;
    }
    qWarning() << "[PromptLoader] 未找到 prompt 文件:" << filename;
    return QString();
}

QString PromptLoader::buildSystemPrompt()
{
    QStringList parts;

    QString core = loadFile("agent_core.md");
    if (!core.isEmpty()) parts << core;

    QString personality = loadFile("enterprise_personality.md");
    if (!personality.isEmpty()) parts << personality;

    QString quotes = loadFile("enterprise_quotes.md");
    if (!quotes.isEmpty()) parts << quotes;

    return parts.join("\n\n");
}
