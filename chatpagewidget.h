#ifndef CHATPAGEWIDGET_H
#define CHATPAGEWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QPixmap>

class ElaScrollArea;
class ElaPlainTextEdit;
class ElaIconButton;
class ElaPushButton;
class ElaScrollPageArea;
class ElaText;
class QVBoxLayout;
class QTextBrowser;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPropertyAnimation;
class QGraphicsOpacityEffect;
class QTimer;
class QElapsedTimer;
class QScrollBar;
class QMenu;
class MessageBubbleWidget;

class ChatPageWidget : public QWidget
{
    Q_OBJECT

public:
    bool isSidebarCollapsed() const { return sidebarCollapsed_; }
    explicit ChatPageWidget(QWidget *parent = nullptr);
    ~ChatPageWidget() override;

    // ---- 消息显示 ----
    void appendMessage(const QString &text, bool isUser, bool showStepIndicator = false);
    void clearChatDisplay();
    void restoreConversation(const QJsonArray &messages);
    void showGreeting();

    // ---- 流式响应 ----
    void onChunkReceived(const QString &delta);
    void onResponseCompleted(const QString &fullText);
    void onResponseError(const QString &errorMessage);

    // ---- 步骤指示器 ----
    void onSpinnerTick();
    void updateAiStep(const QString &text);
    void finishAiStep(bool success, const QString &finalText);

    // ---- 聊天背景 ----
    void applyChatBg(int opacityPercent);
    void setBackgroundPixmap(const QPixmap &pixmap);

    // ---- 侧边栏 ----
    void toggleSidebar();
    void restoreSidebarState(bool collapsed);
    void refreshConversationList(const QJsonArray &meta, const QString &currentId);

    // ---- 输入控制 ----
    void setInputEnabled(bool enabled);
    void setCurrentConversationId(const QString &id);
    void clearAiState();

signals:
    void newConversationClicked();
    void conversationClicked(const QString &id);
    void sendClicked(const QString &text);
    void clearHistoryClicked();
    void conversationDeleteRequested(const QString &id);
    void conversationRenameRequested(const QString &id, const QString &newTitle);
    void sidebarCollapsedChanged(bool collapsed);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUI();
    QString timeBasedGreeting() const;

    // ---- 侧边栏 ----
    QWidget *sidebarWidget_;
    bool sidebarCollapsed_ = false;
    QPropertyAnimation *sidebarAnimation_;
    QPropertyAnimation *sidebarOpacityAnimation_;
    QGraphicsOpacityEffect *sidebarOpacityEffect_;
    QListWidget *historyList_;
    ElaPushButton *clearHistoryBtn_;
    QLabel *historyEmptyLabel_;
    static constexpr int kSidebarExpandedWidth = 260;

    // ---- 消息区域 ----
    ElaScrollArea *messageScrollArea_;
    QWidget *messageContainer_;
    QVBoxLayout *messageLayout_;

    // ---- 输入 ----
    ElaPlainTextEdit *inputEdit_;
    ElaIconButton *sendButton_;

    // ---- AI 内容状态 ----
    QString currentAiBuffer_;
    MessageBubbleWidget *currentAiBubble_ = nullptr;
    QTimer *throttleTimer_ = nullptr;
    void flushAiContent();

    // ---- 步骤指示器 ----
    QTimer *spinnerTimer_ = nullptr;
    int spinnerFrame_ = 0;
    QElapsedTimer *requestElapsed_ = nullptr;

    // ---- 背景 ----
    QPixmap bgPixmap_;
    int currentBgOpacity_ = 25;

    // ---- 当前会话ID ----
    QString currentConversationId_;
};

#endif // CHATPAGEWIDGET_H
