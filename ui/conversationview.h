#ifndef CONVERSATIONVIEW_H
#define CONVERSATIONVIEW_H

#include <QWidget>
#include <QString>
#include <QLabel>
#include <QTimer>

class ElaScrollArea;
class ElaPlainTextEdit;
class ElaIconButton;
class QVBoxLayout;
class QTimer;
class MessageBubbleWidget;

// 聊天消息展示区域的共享实现：消息气泡列表 + 输入框 + 发送/停止按钮 + 流式内容节流刷新。
//
// 被 ChatPageWidget（Chat 模式）和 ProjectPage（Project 模式）共同持有，取代了原来两边
// 各写一份、逻辑几乎一致的 appendMessage / clearChatDisplay / onChunkReceived /
// flushAiContent / setInputEnabled / 输入框回车发送 / 视口尺寸变化 等实现
//（加起来大约 150 行结构性重复代码）。
//
// 设计原则：这个组件只负责"展示"，不知道 AgentEngine、不知道自己是 Chat 模式还是
// Project 模式。发送按钮/回车键只会发出 sendRequested 信号，是否真的调用 AgentEngine、
// 要不要做跨模式占用检查、回复完成后要做什么业务逻辑（比如自动生成标题、同步消息历史），
// 都由外层（ChatPageWidget / ProjectPage）决定。
//
// 页面特有的东西不在这里：
//   - Chat 模式独有的"步骤指示器"（气泡内的 spinner + 文案）仍然由 ChatPageWidget
//     自己管理，因为 Project 模式用的是外部的 ActivityPanel，两者不通用。
//     ChatPageWidget 拿到 appendMessage() 返回的气泡指针后自行叠加步骤指示器逻辑。
//   - "是否处于占用中"（isWaitingResponse_ / isActive_）这类业务状态仍然留在外层，
//     这里只提供 setInputEnabled() 这个纯 UI 操作。
class ConversationView : public QWidget
{
    Q_OBJECT
public:
    explicit ConversationView(QWidget *parent = nullptr);

    // ---- 消息显示 ----
    // 返回新创建的气泡指针，供调用方（比如 Chat 模式）叠加自己的逻辑（如步骤指示器）。
    // isUser == false 且 text 为空时，会显示"思考中"的流式占位动画，并记为"当前 AI 气泡"。
    MessageBubbleWidget *appendMessage(const QString &text, bool isUser);
    void clearChatDisplay();

    // ---- 流式响应 ----
    void onChunkReceived(const QString &delta);
    // 停止节流计时器并立即把缓冲区里剩余的内容刷到 UI。
    // 在收到 responseCompleted / error 时，设置最终内容之前调用，避免丢最后一小段。
    void flushPendingContent();
    MessageBubbleWidget *currentAiBubble() const { return currentAiBubble_; }
    void clearCurrentAiBubble() { currentAiBubble_ = nullptr; }

    // ---- 输入控制 ----
    void setInputEnabled(bool enabled);
    QString inputText() const;
    void clearInput();

    // 自定义发送/停止按钮的边长与输入框高度。
    // Chat 模式和 Project 模式原来这两个尺寸不一样（Project 是三栏布局，空间更紧凑），
    // 合并成一份实现后用这个接口保留各自原来的视觉尺寸，默认值是 Chat 模式原来用的 40/72。
    void setControlSizes(int buttonSize, int inputHeight);

    // ---- 外部偶尔需要访问的底层控件 ----
    ElaScrollArea *scrollArea() const { return messageScrollArea_; }
    QWidget *messageContainer() const { return messageContainer_; }

    void setStatusBarVisible(bool visible);
    void setStatusBarModelName(const QString &model);

signals:
    // 用户点击发送按钮，或在输入框按下 Enter（非 Shift+Enter）
    void sendRequested(const QString &text);
    // 用户点击停止按钮
    void cancelRequested();
    // 消息滚动区 viewport 尺寸变化了（Chat 模式用它来重新适配聊天背景图）
    void viewportResized();
    // 收到本轮回复的第一个流式片段（Chat 模式用它来更新步骤指示器文案）
    void firstChunkOfResponse();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateStatusBarText();
    void setupUI();
    void scrollToBottom();
    void flushAiContent();

    ElaScrollArea *messageScrollArea_ = nullptr;
    QWidget *messageContainer_ = nullptr;
    QVBoxLayout *messageLayout_ = nullptr;

    ElaPlainTextEdit *inputEdit_ = nullptr;
    ElaIconButton *sendButton_ = nullptr;
    ElaIconButton *stopButton_ = nullptr;

    MessageBubbleWidget *currentAiBubble_ = nullptr;
    QString currentAiBuffer_;
    QTimer *throttleTimer_ = nullptr;

    QLabel *statusBarLabel_=nullptr;
    QTimer *statusBarTimer_=nullptr;
    QString statusBarModelName_;
};


#endif // CONVERSATIONVIEW_H
