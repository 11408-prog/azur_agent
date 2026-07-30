#ifndef MODEMANAGER_H
#define MODEMANAGER_H

#include <QObject>
#include <QString>

class ProjectSession;
class ProjectPage;
class ProjectConversationService;
class ConversationManager;

// ModeManager：负责 Chat / Project 双模式的切换逻辑和项目状态管理。
// MainWindow 只负责 UI 搭建和信号连接，模式切换的逻辑全部委托给此类。
class ModeManager : public QObject
{
    Q_OBJECT
public:
    enum class AgentMode { Chat, Project };

    explicit ModeManager(QObject *parent = nullptr);
    ~ModeManager() override;

    // ---- 状态查询 ----
    AgentMode currentMode() const { return currentMode_; }
    ProjectSession *currentProject() const { return currentProject_; }
    QString currentConversationId() const { return currentConversationId_; }

    // ---- 依赖注入（由 MainWindow 在构造时设置） ----
    void setProjectPageWidget(ProjectPage *widget) { projectPageWidget_ = widget; }
    void setConversationService(ProjectConversationService *service) { projectConvService_ = service; }

    // ---- 模式切换 ----
    // parentWidget 用于弹出文件夹选择对话框
    void enterProjectMode(QWidget *parentWidget);
    void enterChatMode();
    void finishProjectInit();

    // ---- 项目对话管理 ----
    void saveConversation();
    void saveEntry();
    void switchToEntry(const QString &path, const QString &convId);
    void switchToConversation(const QString &convId);
    void setConversationId(const QString &id) { currentConversationId_ = id; }

    // 打开一个新项目目录（由"打开文件夹"按钮调用）
    void openProject(const QString &dir);

signals:
    void modeChanged(ModeManager::AgentMode mode);
    void projectPathChanged(const QString &path);
    void conversationIdChanged(const QString &id);

private:
    AgentMode currentMode_ = AgentMode::Chat;
    ProjectSession *currentProject_ = nullptr;
    ProjectPage *projectPageWidget_ = nullptr;
    ProjectConversationService *projectConvService_ = nullptr;
    QString currentConversationId_;
};

#endif // MODEMANAGER_H
