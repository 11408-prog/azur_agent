#include "data/projectconversationservice.h"
#include "data/conversationmanager.h"
#include "data/appsettings.h"

#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QDebug>
#include <QSet>

ProjectConversationService::ProjectConversationService(
    ConversationManager *projectConvMgr,
    ConversationManager *chatConvMgr,
    QObject *parent)
    : QObject(parent)
    , projectConvMgr_(projectConvMgr)
    , chatConvMgr_(chatConvMgr)
{
}

QJsonArray ProjectConversationService::conversationsForProject(
    const QString &projectPath) const
{
    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(projectPath));
    return cleanPath.isEmpty()
        ? projectConvMgr_->conversationsMeta()
        : projectConvMgr_->conversationsForProject(cleanPath);
}

QJsonArray ProjectConversationService::loadConversation(const QString &convId)
{
    return projectConvMgr_->loadConversation(convId);
}

void ProjectConversationService::saveConversation(
    const QString &convId, const QJsonArray &messages, const QString &projectPath)
{
    projectConvMgr_->saveConversation(convId, messages, QString(), projectPath);
}

bool ProjectConversationService::deleteConversation(const QString &id)
{
    return projectConvMgr_->deleteConversation(id);
}

bool ProjectConversationService::renameConversation(const QString &id, const QString &newTitle)
{
    return projectConvMgr_->renameConversation(id, newTitle);
}

QString ProjectConversationService::conversationTitle(const QString &convId) const
{
    const QJsonArray meta = projectConvMgr_->conversationsMeta();
    for (const QJsonValue &v : meta) {
        QJsonObject m = v.toObject();
        if (m["id"].toString() == convId) {
            return m["title"].toString();
        }
    }
    return {};
}

void ProjectConversationService::saveProjectEntry(
    const QString &projectPath, const QString &convId, const QString &convTitle)
{
    if (projectPath.isEmpty() || convId.isEmpty()) return;

    QJsonArray history = AppSettings::projectHistory();

    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(projectPath));
    QJsonObject newEntry;
    newEntry["path"] = cleanPath;
    newEntry["name"] = QDir(cleanPath).dirName();
    newEntry["conversationId"] = convId;
    newEntry["conversationTitle"] = convTitle;
    newEntry["lastOpened"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // 去重
    for (int i = 0; i < history.size(); ++i) {
        if (history[i].toObject()["path"].toString() == cleanPath) {
            history.removeAt(i);
            break;
        }
    }

    history.prepend(newEntry);

    // 最多保留 10 条
    while (history.size() > 10) {
        history.removeLast();
    }

    AppSettings::setProjectHistory(history);
}

QString ProjectConversationService::findEntryConversationId(
    const QString &projectPath)
{
    if (projectPath.isEmpty()) return {};

    const QJsonArray history = AppSettings::projectHistory();
    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(projectPath));

    for (const QJsonValue &val : history) {
        const QJsonObject entry = val.toObject();
        if (entry["path"].toString() == cleanPath) {
            return entry["conversationId"].toString();
        }
    }
    return {};
}

QString ProjectConversationService::resolveConversation(
    const QString &projectPath, const QString &preferredConvId)
{
    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(projectPath));
    const QJsonArray projectMeta = conversationsForProject(cleanPath);

    if (!preferredConvId.isEmpty()) {
        // 检查是否已在项目管理器中
        for (const QJsonValue &val : projectMeta) {
            if (val.toObject()["id"].toString() == preferredConvId) {
                qDebug() << "[ConvService] 使用已有项目对话:" << preferredConvId;
                return preferredConvId;
            }
        }

        // 尝试从聊天管理器迁移
        if (chatConvMgr_) {
            QJsonArray oldMessages = chatConvMgr_->loadConversation(preferredConvId);
            if (!oldMessages.isEmpty()) {
                QString title;
                const QJsonArray chatMeta = chatConvMgr_->conversationsMeta();
                for (const QJsonValue &v : chatMeta) {
                    if (v.toObject()["id"].toString() == preferredConvId) {
                        title = v.toObject()["title"].toString();
                        break;
                    }
                }
                projectConvMgr_->saveConversation(preferredConvId, oldMessages, title, cleanPath);
                chatConvMgr_->deleteConversation(preferredConvId);
                qDebug() << "[ConvService] 从聊天管理器迁移对话:" << preferredConvId;
                return preferredConvId;
            }
        }
    }

    // 新建对话
    QString newId = projectConvMgr_->createNewConversation("项目对话");
    qDebug() << "[ConvService] 创建新项目对话:" << newId;
    return newId;
}

QString ProjectConversationService::createConversation(
    const QString &projectPath)
{
    QString newId = projectConvMgr_->createNewConversation("项目对话");
    if (!projectPath.isEmpty()) {
        // 立即保存空的对话关联到项目
        projectConvMgr_->saveConversation(newId, QJsonArray(), QString(), projectPath);
    }
    return newId;
}

void ProjectConversationService::migrateOldConversations()
{
    if (AppSettings::projectConvMigrationDone()) {
        qDebug() << "[ConvService] 项目对话迁移已完成，跳过";
        return;
    }

    qDebug() << "[ConvService] 开始迁移旧项目对话...";

    // a) 从旧的项目目录 {projectPath}/.azur/data/chats/ 迁移
    const QJsonArray projHistory = AppSettings::projectHistory();
    for (const QJsonValue &val : projHistory) {
        const QJsonObject entry = val.toObject();
        const QString projectPath = entry["path"].toString();
        if (projectPath.isEmpty()) continue;

        QString oldDataDir = QDir::toNativeSeparators(
            QDir::cleanPath(projectPath) + "/.azur/data");
        QString oldChatsDir = oldDataDir + "/chats";

        QDir oldDir(oldChatsDir);
        if (!oldDir.exists()) continue;

        auto *tmpMgr = new ConversationManager(this);
        if (!tmpMgr->initialize(oldDataDir)) {
            qWarning() << "[ConvService] 无法读取旧项目对话目录:" << oldDataDir;
            delete tmpMgr;
            continue;
        }

        const QJsonArray oldMeta = tmpMgr->conversationsMeta();
        for (const QJsonValue &mv : oldMeta) {
            QJsonObject metaObj = mv.toObject();
            QString convId = metaObj["id"].toString();
            QString title = metaObj["title"].toString();
            QJsonArray messages = tmpMgr->loadConversation(convId);
            if (!messages.isEmpty()) {
                projectConvMgr_->saveConversation(convId, messages, title, projectPath);
                qDebug() << "[ConvService] 从项目目录迁移对话:" << convId;
            }
        }
        delete tmpMgr;
    }

    // b) 从全局管理器中的项目对话迁移
    if (chatConvMgr_) {
        const QJsonArray chatMeta = chatConvMgr_->conversationsMeta();
        QSet<QString> projectPaths;
        for (const QJsonValue &val : projHistory) {
            projectPaths.insert(val.toObject()["path"].toString());
        }

        for (const QJsonValue &mv : chatMeta) {
            QJsonObject metaObj = mv.toObject();
            QString convId = metaObj["id"].toString();

            for (const QJsonValue &val : projHistory) {
                QJsonObject entry = val.toObject();
                if (entry["conversationId"].toString() == convId) {
                    QString projectPath = entry["path"].toString();
                    QJsonArray messages = chatConvMgr_->loadConversation(convId);
                    if (!messages.isEmpty()) {
                        projectConvMgr_->saveConversation(convId, messages,
                                                           metaObj["title"].toString(),
                                                           projectPath);
                        qDebug() << "[ConvService] 从全局管理器迁移项目对话:" << convId;
                    }
                    chatConvMgr_->deleteConversation(convId);
                    break;
                }
            }
        }
    }

    AppSettings::setProjectConvMigrationDone(true);
    qDebug() << "[ConvService] 项目对话迁移完成";
}
