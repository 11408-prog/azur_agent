#ifndef PROJECTPAGE_H
#define PROJECTPAGE_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "agent_engine.h"
#include "project_analyzer.h"

#include <ElaIconButton.h>

class QTreeView;
class QFileSystemModel;
class QVBoxLayout;
class ActivityPanel;
class QSplitter;
class QLabel;
class ElaScrollArea;
class ElaPlainTextEdit;
class ElaText;
class DeepSeekClient;
class QElapsedTimer;
class MessageBubbleWidget;
class QPropertyAnimation;
class QGraphicsOpacityEffect;
class QTimer;

// Project Mode 的三栏布局页面：文件树 | Agent对话 | 操作记录
class ProjectPage : public QWidget
{
    Q_OBJECT
public:
    void hideLeftToggleButton() { if (leftToggleBtn_) leftToggleBtn_->setVisible(false); }

public:
    bool isLeftPanelCollapsed() const { return leftPanelCollapsed_; }

signals:
    void leftPanelCollapsedChanged(bool collapsed);

public:
    explicit ProjectPage(AgentEngine *engine, QWidget *parent = nullptr);
    ~ProjectPage() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

public:
    void setProjectPath(const QString &path);
    QString projectPath() const { return projectPath_; }

    void setActive(bool active);
    bool isActive() const { return isActive_; }

    void loadSystemPrompt();

    // ---- 项目索引 ----
    void rebuildIndex();

    // ---- 左右面板折叠（外部可调用，如 MainWindow） ----
    void togglePanel(bool isLeft);

    // 获取当前对话消息列表（供 MainWindow 持久化用）
    QList<QJsonObject> conversation() const { return messageHistory_; }

    // 恢复历史对话（清空显示并加载消息列表）
    void restoreConversation(const QList<QJsonObject> &messages);

signals:
    void sendRequested(const QString &apiKey, const QString &baseUrl,
                       const QString &model, const QJsonArray &messages,
                       const QJsonArray &tools);
    void titleChanged(const QString &title);

    // 对话内容发生变化（发送/收到回复后），供 MainWindow 保存
    void conversationUpdated(const QList<QJsonObject> &messages);

private slots:
    void onSendClicked();
    void onChunkReceived(const QString &delta);
    void onResponseCompleted(const QString &fullText);
    void onError(const QString &errorMessage);

private:
    void setupUI();
    void appendMessage(const QString &text, bool isUser);
    void clearChatDisplay();
    void setInputEnabled(bool enabled);
    void flushAiContent();

    // ---- 左右面板折叠状态持久化 ----
    void restorePanelCollapseState();
    void savePanelCollapseState();

    // ========== UI panels ==========
    QSplitter *splitter_;

    // Left: 文件树
    QWidget *leftPanel_ = nullptr;
    QTreeView *fileTree_;
    QFileSystemModel *fsModel_;

    // Center: Agent对话
    ElaIconButton *leftToggleBtn_ = nullptr;
    ElaIconButton *rightToggleBtn_ = nullptr;
    ElaScrollArea *messageScrollArea_;
    QWidget *messageContainer_;
    QVBoxLayout *messageLayout_;
    ElaPlainTextEdit *inputEdit_;
    ElaIconButton *sendButton_;
    ElaIconButton *stopButton_;
    MessageBubbleWidget *currentAiBubble_ = nullptr;

    // Right: AI 活动面板
    QWidget *rightPanel_ = nullptr;
    ActivityPanel *activityPanel_;

    // ---- 面板折叠动画状态 ----
    QGraphicsOpacityEffect *leftPanelOpacityEffect_ = nullptr;
    QGraphicsOpacityEffect *rightPanelOpacityEffect_ = nullptr;
    QPropertyAnimation *leftPanelWidthAnim_ = nullptr;
    QPropertyAnimation *rightPanelWidthAnim_ = nullptr;
    QPropertyAnimation *leftPanelOpacityAnim_ = nullptr;
    QPropertyAnimation *rightPanelOpacityAnim_ = nullptr;
    bool leftPanelCollapsed_ = false;
    bool rightPanelCollapsed_ = false;
    static constexpr int kLeftPanelWidth = 220;
    static constexpr int kRightPanelWidth = 250;

    // ========== 状态 ==========
    QString projectPath_;
    bool isActive_ = false;
    bool isWaitingResponse_ = false;

    QList<QJsonObject> messageHistory_;
    QString currentAiBuffer_;
    QString lastUserMessage_;

    // ---- 节流程式输出 ----
    QTimer *throttleTimer_ = nullptr;

    AgentEngine *engine_;
    QString systemPrompt_;

    // 项目索引
    QJsonObject projectIndex_;
    QString indexSummary_;

};

#endif // PROJECTPAGE_H