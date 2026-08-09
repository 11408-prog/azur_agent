#ifndef CONVERSATIONMANAGER_H
#define CONVERSATIONMANAGER_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QList>
#include <QLabel>
#include <QTimer>

class ConversationManager : public QObject
{
    Q_OBJECT
public:
    explicit ConversationManager(QObject *parent = nullptr);

    // 如果 customDir 为空，使用默认的 AppData 路径；否则使用指定路径
    bool initialize(const QString &customDir = QString());

    QJsonArray conversationsMeta() const { return conversationsMeta_; }
    QJsonArray loadConversation(const QString &id);
    bool saveConversation(const QString &id, const QJsonArray &messages,
                          const QString &title = QString(),
                          const QString &projectPath = QString());
    QString createNewConversation(const QString &initialTitle = QString());
    bool deleteConversation(const QString &id);
    bool renameConversation(const QString &id, const QString &newTitle);
    QJsonArray conversationsForProject(const QString &projectPath) const;

    QString dataDir() const { return dataDir_; }

signals:
    void conversationListChanged();

private:
    QString dataDir_;
    QJsonArray conversationsMeta_;

    bool loadMetaFromFile();
    bool saveMetaToFile();
    QString generateId() const;
    QString conversationFilePath(const QString &id) const;
    void sortMetaByUpdated();
    QJsonArray backfillMissingTimestamps(const QString &id, const QJsonArray &messages);

};

#endif // CONVERSATIONMANAGER_H