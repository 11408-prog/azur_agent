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

    throttleTimer_ = new QTimer(this);
    throttleTimer_->setSingleShot(true);
    throttleTimer_->setInterval(50);
    connect(throttleTimer_, &QTimer::timeout, this, &ConversationView::flushAiContent);
}

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
    // MODIFIED: 滚动条改为半透明白色，适配暖色壁纸
    messageScrollArea_->setStyleSheet(
        "QScrollArea { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 5px; margin: 0; }"
        "QScrollBar::handle:vertical { background: rgba(120, 120, 130, 0.3); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(120, 120, 130, 0.5); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        );
    messageScrollArea_->viewport()->setStyleSheet("background: transparent;");
    messageScrollArea_->viewport()->installEventFilter(this);
    layout->addWidget(messageScrollArea_, 1);

    QHBoxLayout *inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(10);

    inputEdit_ = new ElaPlainTextEdit(this);
    inputEdit_->setPlaceholderText("输入消息，Enter 发送，Shift+Enter 换行...");
    inputEdit_->setFixedHeight(72);
    inputEdit_->installEventFilter(this);
    // MODIFIED: 输入框改为半透明毛玻璃感
    inputEdit_->setStyleSheet(
        "ElaPlainTextEdit {"
        "   background: rgba(255, 255, 255, 0.72);"
        "   border: 1px solid rgba(255, 255, 255, 0.4);"
        "   border-radius: 14px;"
        "   color: #3a3a4a;"
        "   padding: 10px 14px;"
        "   font-size: 14px;"
        "}"
        "ElaPlainTextEdit:focus {"
        "   border-color: rgba(15, 95, 240, 0.6);"
        "   background: rgba(255, 255, 255, 0.85);"
        "}"
        );
    inputLayout->addWidget(inputEdit_, 1);

    sendButton_ = new ElaIconButton(ElaIconType::PaperPlane, 18, 40, 40, this);
    sendButton_->setToolTip("发送 (Enter)");
    // MODIFIED: 发送按钮改为暖珊瑚色
    sendButton_->setStyleSheet(
        "ElaIconButton {"
        "   background: rgba(15, 95, 240, 0.85);"
        "   border: none;"
        "   border-radius: 12px;"
        "   color: white;"
        "}"
        "ElaIconButton:hover {"
        "   background: rgba(13, 82, 210, 0.95);"
        "}"
        );
    connect(sendButton_, &ElaIconButton::clicked, this, [this]() {
        emit sendRequested(inputEdit_->toPlainText().trimmed());
    });
    inputLayout->addWidget(sendButton_, 0, Qt::AlignBottom);

    stopButton_ = new ElaIconButton(ElaIconType::Ban, 18, 40, 40, this);
    stopButton_->setToolTip("停止生成");
    stopButton_->setVisible(false);
    // MODIFIED: 停止按钮样式
    stopButton_->setStyleSheet(
        "ElaIconButton {"
        "   background: rgba(80, 80, 90, 0.7);"
        "   border: none;"
        "   border-radius: 12px;"
        "   color: #f0f0f0;"
        "}"
        );
    connect(stopButton_, &ElaIconButton::clicked, this, [this]() {
        emit cancelRequested();
    });
    inputLayout->addWidget(stopButton_, 0, Qt::AlignBottom);

    layout->addLayout(inputLayout);

    // ---- 底部状态栏 ----
    statusBarLabel_ = new QLabel(this);
    statusBarLabel_->setFixedHeight(26);
    statusBarLabel_->setStyleSheet(
        "QLabel {"
        "  font-size: 11px;"
        "  color: #a0a0a0;"
        "  padding: 0 4px;"
        "  background: transparent;"
        "}"
        );
    statusBarLabel_->setAlignment(Qt::AlignVCenter);
    layout->addWidget(statusBarLabel_);

    // 每分钟刷新时间
    statusBarTimer_ = new QTimer(this);
    connect(statusBarTimer_, &QTimer::timeout, this, [this]() {
        updateStatusBarText();
    });
    statusBarTimer_->start(60000); // 60秒
    updateStatusBarText();
}

MessageBubbleWidget *ConversationView::appendMessage(const QString &text, bool isUser)
{
    MessageBubbleWidget *bubble = new MessageBubbleWidget(isUser, messageContainer_);
    bubble->setTimestamp(QDateTime::currentDateTime().toString("HH:mm"));

    if (isUser) {
        bubble->setUserContent(text);
    } else {
        if (!text.isEmpty()) {
            bubble->setAiContent(text);
        } else {
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

void ConversationView::onChunkReceived(const QString &delta)
{
    if (!currentAiBubble_) return;
    const bool wasFirstChunk = currentAiBuffer_.isEmpty();
    currentAiBuffer_ += delta;
    if (wasFirstChunk) {
        emit firstChunkOfResponse();
    }
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

bool ConversationView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == inputEdit_ && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            emit sendRequested(inputEdit_->toPlainText().trimmed());
            return true;
        }
    }

    if (watched == messageScrollArea_->viewport() && event->type() == QEvent::Resize) {
        emit viewportResized();
        return false;
    }

    return QWidget::eventFilter(watched, event);
}

void ConversationView::updateStatusBarText()
{
    if (!statusBarLabel_) return;
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm");
    QString modelStr = statusBarModelName_.isEmpty() ? QStringLiteral("就绪") : statusBarModelName_;
    statusBarLabel_->setText(QString("%1  ·  %2").arg(modelStr, timeStr));
}

void ConversationView::setStatusBarVisible(bool visible)
{
    if (statusBarLabel_) statusBarLabel_->setVisible(visible);
}

void ConversationView::setStatusBarModelName(const QString &model)
{
    statusBarModelName_ = model;
    updateStatusBarText();
}