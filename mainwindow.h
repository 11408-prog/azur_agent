#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <ElaWindow.h>
#include <ElaIconButton.h>
#include <ElaScrollPage.h>
#include <ElaScrollArea.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaListView.h>
#include <ElaMessageBar.h>
#include <ElaTheme.h>
#include <ElaLineEdit.h>
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaIcon.h>

#include <QSlider>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QCloseEvent>
#include <QSettings>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextBrowser>
#include <QDateTime>
#include <QStandardPaths>
#include <QFile>
#include <QMenu>
#include <QTimer>
#include <QElapsedTimer>
#include <QUrl>

#include "ai_client.h"
#include "conversationmanager.h"
#include "agent_engine.h"

class ProjectPage;
class ProjectSession;
class ChatPageWidget;
class SettingPageWidget;

class MainWindow : public ElaWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onNewConversation();
    void loadConversation(const QString &id);

    void onSendClicked();
    void onSelectWorkspaceClicked();

    void onApiChunkReceived(const QString &delta);
    void onApiResponseCompleted(const QString &fullText);
    void onApiError(const QString &errorMessage);

public:
    enum class AgentMode { Chat, Project };

private:
    // 根据当前模式更新按钮图标和tooltip
    void updateToggleButtonState();
    // ---- 模式切换 ----
    void enterProjectMode();
    void enterChatMode();
    void finishProjectInit();      // 延迟执行的项目初始化（索引重建等重任务）

    // ---- UI 构建 ----
    void setupNavigation();
    void setupAboutPage();
    void restoreSidebarState(bool collapsed);

    // ---- 设置持久化（仅保留 MainWindow 关注的 key） ----
    void saveSettings();
    void loadSettings();

    // ---- Prompt 构建 ----
    QString buildSystemPrompt() const;
    QString loadPromptFile(const QString &filename) const;

    // ---- 导航页 ----
    ElaScrollPage *chatPage_;
    ElaScrollPage *chatHistoryPage_;
    ElaScrollPage *settingPage_;
    ElaScrollPage *aboutPage_;

    // ========== 以下成员已提取到 ChatPageWidget ==========
    // ElaScrollArea *messageScrollArea_;
    // QWidget *messageContainer_;
    // QVBoxLayout *messageLayout_;
    // ElaPlainTextEdit *inputEdit_;
    // ElaIconButton *sendButton_;
    // QListWidget *historyList_;
    // ElaPushButton *clearHistoryBtn_;
    // QLabel *historyEmptyLabel_;
    // QString lastUserMessage_;
    // QString currentAiBuffer_;
    // QTextBrowser *currentAiContent_;
    // QWidget *currentStepRow_;
    // QLabel *currentStepIcon_;
    // QLabel *currentStepText_;
    // QTimer *spinnerTimer_;
    // int spinnerFrame_;
    // QElapsedTimer requestElapsed_;
    // QWidget *sidebarWidget_;
    // bool sidebarCollapsed_ = false;
    // ElaIconButton *toggleSidebarBtn_;
    // QPropertyAnimation *sidebarAnimation_;
    // QPropertyAnimation *sidebarOpacityAnimation_;
    // QGraphicsOpacityEffect *sidebarOpacityEffect_;
    // static constexpr int kSidebarExpandedWidth = 260;
    // QSlider *bgOpacitySlider_;
    // QPixmap bgPixmap_;

    // ========== 以下成员已提取到 SettingPageWidget ==========
    // ElaLineEdit *apiKeyEdit_;
    // ElaLineEdit *baseUrlEdit_;
    // ElaComboBox *modelComboBox_;
    // QLabel *workspaceLabel_;
    // ElaPushButton *selectWorkspaceBtn_;
    // QStringList recentModels_;

    // ---- 业务成员（保留） ----
    QString historyFilePath_;
    QJsonArray historyEntries_;
    QString lastUserMessage_;
    DeepSeekClient *client_;
    QList<QJsonObject> messageHistory_;
    bool isWaitingResponse_;

    ConversationManager *conversationManager_;
    ConversationManager *projectConvMgr_; // 项目模式专用的会话管理器（共享实例）
    QString currentConversationId_;
    QString systemPrompt_;

    // Agent引擎
    AgentEngine *chatEngine_;

    // ---- 新解耦组件 ----
    ChatPageWidget *chatPageWidget_;
    SettingPageWidget *settingsPageWidget_;

    // ---- AppBar 按钮 ----
    ElaIconButton *sidebarToggleBtn_;
    ElaIconButton *openFolderBtn_;
    ElaIconButton *projectHistoryBtn_;
    ElaIconButton *projectConvListBtn_;    // 当前项目的对话列表

    // ---- 项目历史记录 ----
    void saveCurrentProjectEntry();
    void switchToProjectEntry(const QString &path, const QString &convId);
    void saveProjectConversation();

    // ---- 一次性迁移 ----
    void migrateOldProjectConversations();

    // ---- 项目对话列表 ----
    void switchToProjectConversation(const QString &convId);

    // ---- 双模式 ----
    ElaScrollPage *projectPage_;
    ProjectPage *projectPageWidget_;
    AgentMode currentMode_ = AgentMode::Chat;
    ProjectSession *currentProject_ = nullptr;
};

#endif // MAINWINDOW_H
