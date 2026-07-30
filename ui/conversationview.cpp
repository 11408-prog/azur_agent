#include "ui/conversationview.h"
#include "ui/messagebubblewidget.h"

#include <ElaScrollArea.h>
#include <ElaPlainTextEdit.h>
#include <ElaIconButton.h>
#include <ElaIcon.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QTimer>
#include <QKeyEvent>
#include <QDateTime>

ConversationView::ConversationView(QWidget *parent)
    : QWidget(parent)
{
    setupUI();

    // 节流流式输出：每 50ms 刷新一次 UI，避免逐 token 都触发重排版
    throttleTimer_ = new QTimer(this);
    throttleTimer_->setSingleShot(true);
    throttleTimer_->setInterval(50);
    connect(throttleTimer_, &QTimer::timeout, this, &ConversationView::flushAiContent);
}

// ==================== UI 构建 ====================
void ConversationView::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    messageContainer_ = new QWidget();
    messageLayout_ = new QVBoxLayout(messageContainer_);
    messageLayout_->setContentsMargins(0, 0, 0, 0);
    messageLayout_->setSpacing(10);
    messageLayout_->addStretch();

    messageScrollArea_ = new ElaScrollArea(this);
    messageScrollArea_->setWidget(messageContainer_);
    messageScrollArea_->setWidgetResizable(true);
    messageScrollArea_->setFrameShape(QFrame::NoFrame);
    messageScrollArea_->setIsGrabGesture(true);
    messageScrollArea_->setIsOverShoot(Qt::Vertical, true);
    messageScrollArea_->setIsAnimation(Qt::Vertical, true);

    messageScrollArea_->setVerticalScrollBar(new QScrollBar(Qt::Vertical, messageScrollArea_));
    messageScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    messageScrollArea_->setStyleSheet(
        "QScrollArea { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: rgba(0,0,0,0.18); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(0,0,0,0.32); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
    );
    messageScrollArea_->viewport()->setStyleSheet("background: transparent;");
    messageScrollArea_->viewport()->installEventFilter(this);
    layout->addWidget(messageScrollArea_, 1);

    // 输入区域
    QHBoxLayout *inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(10);

    inputEdit_ = new ElaPlainTextEdit(this);
    inputEdit_->setPlaceholderText("输入消息，Enter 发送，Shift+Enter 换行...");
    inputEdit_->setFixedHeight(72);
    inputEdit_->installEventFilter(this);
    inputLayout->addWidget(inputEdit_, 1);

    sendButton_ = new ElaIconButton(ElaIconType::PaperPlane, 18, 40, 40, this);
    sendButton_->setToolTip("发送 (Enter)");
    connect(sendButton_, &ElaIconButton::clicked, this, [this]() {
        emit sendRequested(inputEdit_->toPlainText().trimmed());
    });
    inputLayout->addWidget(sendButton_, 0, Qt::AlignBottom);

    stopButton_ = new ElaIconButton(ElaIconType::Ban, 18, 40, 40, this);
    stopButton_->setToolTip("停止生成");
    stopButton_->setVisible(false);
    connect(stopButton_, &ElaIconButton::clicked, this, [this]() {
        emit cancelRequested();
    });
    inputLayout->addWidget(stopButton_, 0, Qt::AlignBottom);

    layout->addLayout(inputLayout);
}

// ==================== 消息气泡 ====================
MessageBubbleWidget *ConversationView::appendMessage(const QString &text, bool isUser)
{
    MessageBubbleWidget *bubble = new MessageBubbleWidget(isUser, messageContainer_);
    bubble->setTimestamp(QDateTime::currentDateTime().toString("HH:mm"));

    if (isUser) {
        bubble->setUserContent(text);
    } else {
        if (!text.isEmpty()) {
            // 有内容：恢复历史或完整回复，直接渲染
            bubble->setAiContent(text);
        } else {
            // 无内容：流式占位，显示旋转动画
            bubble->setAiStreamingContent(QStringLiteral("思考中 ⠋"));
            bubble->startContentSpinner();
        }
        currentAiBubble_ = bubble;
        currentAiBuffer_.clear();
    }

    messageLayout_->insertWidget(messageLayout_->count() - 1, bubble);
    scrollToBottom();

    return bubble;
}

void ConversationView::clearChatDisplay()
{
    currentAiBubble_ = nullptr;
    currentAiBuffer_.clear();
    while (messageLayout_->count() > 1) {
        QLayoutItem *item = messageLayout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void ConversationView::scrollToBottom()
{
    QScrollBar *vBar = messageScrollArea_->verticalScrollBar();
    QMetaObject::invokeMethod(this, [vBar]() {
        vBar->setValue(vBar->maximum());
    }, Qt::QueuedConnection);
}

// ==================== 流式响应 ====================
void ConversationView::onChunkReceived(const QString &delta)
{
    if (!currentAiBubble_) return;
    const bool wasFirstChunk = currentAiBuffer_.isEmpty();
    currentAiBuffer_ += delta;
    if (wasFirstChunk) {
        emit firstChunkOfResponse();
    }
    // 节流：50ms 内多个 token 只触发一次 UI 刷新
    if (!throttleTimer_->isActive()) {
        throttleTimer_->start();
    }
}

void ConversationView::flushAiContent()
{
    if (!currentAiBubble_) return;
    currentAiBubble_->setAiStreamingContent(currentAiBuffer_);
    QScrollBar *vBar = messageScrollArea_->verticalScrollBar();
    vBar->setValue(vBar->maximum());
}

void ConversationView::flushPendingContent()
{
    if (throttleTimer_->isActive()) {
        throttleTimer_->stop();
        flushAiContent();
    }
}

// ==================== 输入控制 ====================
void ConversationView::setInputEnabled(bool enabled)
{
    inputEdit_->setEnabled(enabled);
    sendButton_->setVisible(enabled);
    sendButton_->setEnabled(enabled);
    stopButton_->setVisible(!enabled);
    if (enabled) inputEdit_->setFocus();
}

QString ConversationView::inputText() const
{
    return inputEdit_->toPlainText().trimmed();
}

void ConversationView::clearInput()
{
    inputEdit_->clear();
}

void ConversationView::setControlSizes(int buttonSize, int inputHeight)
{
    sendButton_->setFixedSize(buttonSize, buttonSize);
    stopButton_->setFixedSize(buttonSize, buttonSize);
    inputEdit_->setFixedHeight(inputHeight);
}

// ==================== 事件过滤 ====================
bool ConversationView::eventFilter(QObject *watched, QEvent *event)
{
    // 输入框按 Enter（非 Shift+Enter）发送
    if (watched == inputEdit_ && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            emit sendRequested(inputEdit_->toPlainText().trimmed());
            return true;
        }
    }

    // 消息滚动区视口尺寸变化（Chat 模式用来重新适配聊天背景图）
    if (watched == messageScrollArea_->viewport() && event->type() == QEvent::Resize) {
        emit viewportResized();
        return false;
    }

    return QWidget::eventFilter(watched, event);
}
