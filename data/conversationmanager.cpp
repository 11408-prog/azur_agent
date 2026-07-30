#include "data/conversationmanager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QDateTime>
#include <QStandardPaths>
#include <QRandomGenerator>
#include <algorithm>
#include <QDebug>

ConversationManager::ConversationManager(QObject *parent)
    : QObject(parent)
{
}

bool ConversationManager::initialize(const QString &customDir)
{
    if (customDir.isEmpty()) {
        QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        dataDir_ = appData + "/AzurAgent/data";
    } else {
        dataDir_ = customDir;
    }
    QDir dir;
    if (!dir.mkpath(dataDir_)) {
        qWarning() << "Failed to create data directory:" << dataDir_;
        return false;
    }
    // 创建 chats 子目录
    QString chatsDir = dataDir_ + "/chats";
    if (!dir.mkpath(chatsDir)) {
        qWarning() << "Failed to create chats directory:" << chatsDir;
        return false;
    }
    // 重新加载元数据
    conversationsMeta_ = QJsonArray();
    return loadMetaFromFile();
}

bool ConversationManager::loadMetaFromFile()
{
    QFile file(dataDir_ + "/conversations.json");
    if (!file.exists()) {
        conversationsMeta_ = QJsonArray();
        return saveMetaToFile();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open conversations.json for reading";
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (doc.isArray()) {
        conversationsMeta_ = doc.array();
        sortMetaByUpdated();
        return true;
    }
    conversationsMeta_ = QJsonArray();
    return saveMetaToFile();
}

bool ConversationManager::saveMetaToFile()
{
    QFile file(dataDir_ + "/conversations.json");
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot write conversations.json";
        return false;
    }
    QJsonDocument doc(conversationsMeta_);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QString ConversationManager::generateId() const
{
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    int rand = QRandomGenerator::global()->bounded(1000);
    return QString("%1-%2").arg(ts).arg(rand, 3, 10, QChar('0'));
}

QString ConversationManager::conversationFilePath(const QString &id) const
{
    return dataDir_ + "/chats/" + id + ".json";
}

void ConversationManager::sortMetaByUpdated()
{
    QList<QJsonObject> list;
    for (const QJsonValue &val : conversationsMeta_) {
        list.append(val.toObject());
    }
    std::sort(list.begin(), list.end(),
              [](const QJsonObject &a, const QJsonObject &b) {
                  return a["updated"].toString() > b["updated"].toString();
              });
    conversationsMeta_ = QJsonArray();
    for (const QJsonObject &obj : list) {
        conversationsMeta_.append(obj);
    }
}

QJsonArray ConversationManager::loadConversation(const QString &id)
{
    QString path = conversationFilePath(id);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open conversation file:" << path;
        // 如果文件不存在，从元信息中删除该会话（避免残留）
        for (int i = 0; i < conversationsMeta_.size(); ++i) {
            if (conversationsMeta_[i].toObject()["id"].toString() == id) {
                conversationsMeta_.removeAt(i);
                saveMetaToFile();
                break;
            }
        }
        return QJsonArray();
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (doc.isObject()) {
        return doc.object()["messages"].toArray();
    }
    return QJsonArray();
}

bool ConversationManager::saveConversation(const QString &id, const QJsonArray &messages,
                                           const QString &title,
                                           const QString &projectPath)
{
    // 确保 chats 目录存在（防止意外删除）
    QDir dir;
    QString chatsDir = dataDir_ + "/chats";
    if (!dir.mkpath(chatsDir)) {
        qWarning() << "Failed to create chats directory for saving:" << chatsDir;
        return false;
    }

    bool found = false;
    for (int i = 0; i < conversationsMeta_.size(); ++i) {
        QJsonObject meta = conversationsMeta_[i].toObject();
        if (meta["id"].toString() == id) {
            if (!title.isEmpty()) {
                meta["title"] = title;
            }
            meta["updated"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            meta["messageCount"] = messages.size();
            if (!projectPath.isEmpty()) {
                meta["project"] = QDir::toNativeSeparators(QDir::cleanPath(projectPath));
            }
            conversationsMeta_[i] = meta;
            found = true;
            break;
        }
    }
    if (!found) {
        QJsonObject meta;
        meta["id"] = id;
        meta["title"] = title.isEmpty() ? "新对话" : title;
        meta["created"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        meta["updated"] = meta["created"];
        meta["messageCount"] = messages.size();
        if (!projectPath.isEmpty()) {
            meta["project"] = QDir::toNativeSeparators(QDir::cleanPath(projectPath));
        }
        conversationsMeta_.append(meta);
    }
    sortMetaByUpdated();
    if (!saveMetaToFile()) {
        return false;
    }

    QString path = conversationFilePath(id);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot write conversation file:" << path;
        return false;
    }
    QJsonObject obj;
    obj["id"] = id;
    obj["messages"] = messages;
    QJsonDocument doc(obj);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    emit conversationListChanged();
    return true;
}

QString ConversationManager::createNewConversation(const QString &initialTitle)
{
    QString id = generateId();
    QJsonArray empty;
    if (!saveConversation(id, empty, initialTitle.isEmpty() ? "新对话" : initialTitle)) {
        qWarning() << "Failed to create new conversation:" << id;
        return QString();
    }
    return id;
}

bool ConversationManager::deleteConversation(const QString &id)
{
    qDebug()<<"[CONV_MGR] deleteConversation | id="<<id;
    for (int i = 0; i < conversationsMeta_.size(); ++i) {
        if (conversationsMeta_[i].toObject()["id"].toString() == id) {
            conversationsMeta_.removeAt(i);
            break;
        }
    }
    if (!saveMetaToFile()) {
        return false;
    }
    QFile::remove(conversationFilePath(id));
    emit conversationListChanged();
    return true;
}

bool ConversationManager::renameConversation(const QString &id, const QString &newTitle)
{
    qDebug()<<"[CONV_MGR] renameConversation | id="<<id<<"|newTitle="<<newTitle;
    for (int i = 0; i < conversationsMeta_.size(); ++i) {
        QJsonObject meta = conversationsMeta_[i].toObject();
        if (meta["id"].toString() == id) {
            meta["title"] = newTitle;
            conversationsMeta_[i] = meta;
            saveMetaToFile();
            emit conversationListChanged();
            return true;
        }
    }
    return false;
}

QJsonArray ConversationManager::conversationsForProject(const QString &projectPath) const
{
    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(projectPath));
    QJsonArray result;
    for (const QJsonValue &val : conversationsMeta_) {
        const QJsonObject meta = val.toObject();
        if (meta["project"].toString() == cleanPath) {
            result.append(meta);
        }
    }
    return result;
}