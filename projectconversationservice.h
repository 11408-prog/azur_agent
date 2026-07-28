#ifndef PROJECTCONVERSATIONSERVICE_H
#define PROJECTCONVERSATIONSERVICE_H

#include <QObject>
#include <QJsonArray>

class ConversationManager;

// 项目对话管理服务层
// 封装了项目对话的保存、加载、切换、迁移等业务逻辑，
// 使 MainWindow 从数据操作中解耦，只需关注 UI 协调。
class ProjectConversationService : public QObject
{
    Q_OBJECT
public:
    explicit ProjectConversationService(ConversationManager *projectConvMgr,
                                        ConversationManager *chatConvMgr,
                                        QObject *parent = nullptr);

    // ---- 基础 CRUD ----
    QJsonArray conversationsForProject(const QString &projectPath) const;
    QJsonArray loadConversation(const QString &convId);
    void saveConversation(const QString &convId, const QJsonArray &messages,
                          const QString &projectPath);
    bool deleteConversation(const QString &id);
    bool renameConversation(const QString &id, const QString &newTitle);
    QString conversationTitle(const QString &convId) const;

    // ---- 项目入口管理（QSettings 持久化） ----
    // 将项目入口记录写入 QSettings projectHistory
    static void saveProjectEntry(const QString &projectPath,
                                  const QString &convId,
                                  const QString &convTitle);
    // 从 QSettings projectHistory 查找项目的对话 ID
    static QString findEntryConversationId(const QString &projectPath);

    // ---- 对话解析 ----
    // 解析/创建目标项目的对话：优先使用 preferredConvId，
    // 不存在则尝试从聊天管理器迁移，仍不存在则新建
    QString resolveConversation(const QString &projectPath,
                                 const QString &preferredConvId);
    // 创建新对话并绑定项目
    QString createConversation(const QString &projectPath);

    // ---- 迁移 ----
    void migrateOldConversations();

signals:
    void conversationListChanged();

private:
    ConversationManager *projectConvMgr_;
    ConversationManager *chatConvMgr_;
};

#endif // PROJECTCONVERSATIONSERVICE_H
