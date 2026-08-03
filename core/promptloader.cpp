#include "core/promptloader.h"

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

    QString core = loadFile("full/agent_core.md");
    if (!core.isEmpty()) parts << core;

    QString personality = loadFile("full/enterprise_personality.md");
    if (!personality.isEmpty()) parts << personality;

    QString quotes = loadFile("full/enterprise_quotes.md");
    if (!quotes.isEmpty()) parts << quotes;

    return parts.join("\n\n");
}

QString PromptLoader::buildChatSystemPrompt(int style)
{
    // style: 0 = 精简人格（本地小模型），1 = 完整人格（云端大模型）
    if (style == 1) {
        return buildSystemPrompt();
    }

    // 聊天模式默认只加载精简人格，不再拼接多个文件
    QString content = loadFile("lite/enterprise.md");
    if (content.isEmpty()) {
        qWarning() << "[PromptLoader] lite/enterprise.md 未找到，使用默认提示";
        return QStringLiteral("你是企业，碧蓝航线的航空母舰。用简短平静的语气说话。");
    }
    return content;
}
