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
    // 兜底：源码树路径不存在时（比如打包分发后），从编译进二进制的 qrc
    // 资源里读取。app.qrc 里的路径需要和这里请求的 filename 完全一致
    // （含 full/、lite/ 子目录前缀），否则这个兜底会静默失效。
    QFile resFile(":/prompts/" + filename);
    if (resFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(resFile.readAll()).trimmed();
        return content;
    }
    qWarning() << "[PromptLoader] 未找到 prompt 文件:" << filename;
    return QString();
}

QString PromptLoader::buildFullPersonaPrompt()
{
    QStringList parts;

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
        return buildFullPersonaPrompt();
    }

    QString content = loadFile("lite/enterprise.md");
    if (content.isEmpty()) {
        qWarning() << "[PromptLoader] lite/enterprise.md 未找到，使用默认提示";
        return QStringLiteral("你是企业，碧蓝航线的航空母舰。用简短平静的语气说话。");
    }
    return content;
}

QString PromptLoader::buildPostHistoryInstructions(int style)
{
    // style: 0 = 精简，1 = 完整。与 buildChatSystemPrompt 的分档保持一致。
    if (style == 1) {
        QString content = loadFile("full/enterprise_instructions.md");
        if (!content.isEmpty()) return content;
        qWarning() << "[PromptLoader] full/enterprise_instructions.md 未找到";
        return QString();
    }

    QString content = loadFile("lite/enterprise_instructions.md");
    if (!content.isEmpty()) return content;
    qWarning() << "[PromptLoader] lite/enterprise_instructions.md 未找到";
    return QString();
}
