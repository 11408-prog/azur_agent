#include "ui/conversationview.h"
#include "ui/messagebubblewidget.h"
#include "ui/theme.h"

#include <ElaScrollArea.h>
#include <ElaPlainTextEdit.h>
#include <ElaIconButton.h>
#include <ElaIcon.h>
#include <ElaTheme.h>

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
    messageScrollArea_->viewport()->installEventFilter(this);
    layout->addWidget(messageScrollArea_, 1);

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

    // ---- 底部状态栏 ----
    statusBarLabel_ = new QLabel(this);
    statusBarLabel_->setFixedHeight(26);
    statusBarLabel_->setAlignment(Qt::AlignVCenter);
    layout->addWidget(statusBarLabel_);

    // 每分钟刷新时间
    statusBarTimer_ = new QTimer(this);
    connect(statusBarTimer_, &QTimer::timeout, this, [this]() {
        updateStatusBarText();
    });
    statusBarTimer_->start(60000); // 60秒
    updateStatusBarText();

    connect(eTheme, &ElaTheme::themeModeChanged, this, [this](ElaThemeType::ThemeMode) {
        applyTheme();
    });
    applyTheme();
}

void ConversationView::applyTheme()
{
    // 滚动区
    const QColor handleCol = UiTheme::over(UiTheme::textPrimary(), UiTheme::surface(),
                                           UiTheme::dark() ? 0.35 : 0.22);
    const QColor handleHover = UiTheme::over(UiTheme::textPrimary(), UiTheme::surface(),
                                             UiTheme::dark() ? 0.55 : 0.40);
    messageScrollArea_->setStyleSheet(QString(
        "QScrollArea { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 5px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %1; border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: %2; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        )
        .arg(UiTheme::qss(handleCol), UiTheme::qss(handleHover)));
    messageScrollArea_->viewport()->setStyleSheet("background: transparent;");

    // 输入框：实色 surface + 边框，focus 时 accent 边框，圆角 12
    inputEdit_->setStyleSheet(QString(
        "ElaPlainTextEdit {"
        "   background: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 12px;"
        "   color: %3;"
        "   padding: 10px 14px;"
        "   font-size: 14px;"
        "}"
        "ElaPlainTextEdit:focus {"
        "   border-color: %4;"
        "}"
        )
        .arg(UiTheme::qss(UiTheme::surface()),
             UiTheme::qss(UiTheme::border()),
             UiTheme::qss(UiTheme::textPrimary()),
             UiTheme::qss(UiTheme::accent())));

    // 发送按钮：accent 实心 + hover accentHover
    sendButton_->setStyleSheet(QString(
        "ElaIconButton {"
        "   background: %1;"
        "   border: none;"
        "   border-radius: 12px;"
        "   color: %2;"
        "}"
        "ElaIconButton:hover { background: %3; }"
        )
        .arg(UiTheme::qss(UiTheme::accent()),
             UiTheme::qss(UiTheme::textOnAccent()),
             UiTheme::qss(UiTheme::accentHover())));

    // 停止按钮：中性灰
    const QColor stopCol = UiTheme::over(UiTheme::textSecondary(), UiTheme::surface(), 0.85);
    stopButton_->setStyleSheet(QString(
        "ElaIconButton {"
        "   background: %1;"
        "   border: none;"
        "   border-radius: 12px;"
        "   color: %2;"
        "}"
        "ElaIconButton:hover { background: %3; }"
        )
        .arg(UiTheme::qss(stopCol),
             UiTheme::qss(UiTheme::textPrimary()),
             UiTheme::qss(UiTheme::hoverOverlay())));

    // 状态栏文字
    statusBarLabel_->setStyleSheet(QString(
        "QLabel {"
        "  font-size: 11px;"
        "  color: %1;"
        "  padding: 0 4px;"
        "  background: transparent;"
        "}"
        )
        .arg(UiTheme::qss(UiTheme::textSecondary())));
}

MessageBubbleWidget *ConversationView::appendMessage(const QString &text, bool isUser,
                                                       const QDateTime &timestamp)
{
    MessageBubbleWidget *bubble = new MessageBubbleWidget(isUser, messageContainer_);
    // 传入的时间戳无效（默认参数没传，代表这是一条刚发生的新消息）就用当前时间；
    // 加载历史消息时会传入消息真实创建时的时间，不能让它在这里被重新赋值成"现在"，
    // 否则每次重新打开一个老对话，所有历史消息都会显示成刚刚发的。
    const QDateTime ts = timestamp.isValid() ? timestamp : QDateTime::currentDateTime();
    bubble->setTimestamp(ts.toString("yyyy/M/d HH:mm"));

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