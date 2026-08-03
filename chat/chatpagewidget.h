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
class ConversationView;

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
    void clearChatBg();

    // ---- 侧边栏 ----
    void toggleSidebar();
    void restoreSidebarState(bool collapsed);
    void refreshConversationList(const QJsonArray &meta, const QString &currentId);

    // ---- 输入控制 ----
    void setInputEnabled(bool enabled);
    void setCurrentConversationId(const QString &id);
    void clearAiState();

    // 用户取消生成时调用：把当前正卡在"思考中/正在连接..."占位动画的气泡
    // 收尾成"已取消生成"的明确状态，而不是让它永远卡在思考动画里。
    void cancelAiResponse();

signals:
    // 用户点击取消生成按钮
    void cancelRequested();

    void newConversationClicked();
    void conversationClicked(const QString &id);
    void sendClicked(const QString &text);
    void clearHistoryClicked();
    void conversationDeleteRequested(const QString &id);
    void conversationRenameRequested(const QString &id, const QString &newTitle);
    void sidebarCollapsedChanged(bool collapsed);

private:
    void setupUI();
    QString timeBasedGreeting() const;

    // ---- 侧边栏 ----
    QWidget *sidebarWidget_;
    bool sidebarCollapsed_ = true;
    QPropertyAnimation *sidebarAnimation_;
    QPropertyAnimation *sidebarOpacityAnimation_;
    QGraphicsOpacityEffect *sidebarOpacityEffect_;
    QListWidget *historyList_;
    ElaPushButton *clearHistoryBtn_;
    QLabel *historyEmptyLabel_;
    static constexpr int kSidebarExpandedWidth = 260;

    // ---- 消息区域 + 输入区域（共享组件，见 ui/conversationview.h） ----
    ConversationView *conversationView_ = nullptr;

    // ---- 步骤指示器（Chat 模式独有，Project 模式用的是外部 ActivityPanel，两者不通用） ----
    QTimer *spinnerTimer_ = nullptr;
    int spinnerFrame_ = 0;
    QElapsedTimer *requestElapsed_ = nullptr;

    // ---- 背景 ----
    QPixmap bgPixmap_;
    int currentBgOpacity_ = 25;

    // ---- 当前会话ID ----
    QString currentConversationId_;

protected:
    //void resizeEvent(QResizeEvent *event) override;
private:
    QWidget *chatArea_=nullptr;
};

#endif // CHATPAGEWIDGET_H
