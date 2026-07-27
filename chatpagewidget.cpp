#include "chatpagewidget.h"
#include "markdownrenderer.h"
#include "messagebubblewidget.h"

#include <ElaScrollArea.h>
#include <ElaPlainTextEdit.h>
#include <ElaIconButton.h>
#include <ElaPushButton.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaMessageBar.h>
#include <ElaIcon.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QElapsedTimer>
#include <QScrollBar>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QLineEdit>
#include <QKeyEvent>
#include <QDateTime>
#include <QTime>
#include <QPainter>
#include <QPalette>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QGuiApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>

ChatPageWidget::ChatPageWidget(QWidget *parent)
    : QWidget(parent)
    , sidebarWidget_(nullptr)
    , sidebarAnimation_(nullptr)
    , sidebarOpacityAnimation_(nullptr)
    , sidebarOpacityEffect_(nullptr)
    , historyList_(nullptr)
    , clearHistoryBtn_(nullptr)
    , historyEmptyLabel_(nullptr)
    , messageScrollArea_(nullptr)
    , messageContainer_(nullptr)
    , messageLayout_(nullptr)
    , inputEdit_(nullptr)
    , sendButton_(nullptr)
    , spinnerTimer_(nullptr)
    , spinnerFrame_(0)
    , requestElapsed_(nullptr)
    , currentBgOpacity_(25)
{
    setupUI();

    // 节流程式输出：每 50ms 刷新一次 UI
    throttleTimer_ = new QTimer(this);
    throttleTimer_->setSingleShot(true);
    throttleTimer_->setInterval(50);
    connect(throttleTimer_, &QTimer::timeout, this, &ChatPageWidget::flushAiContent);
}

ChatPageWidget::~ChatPageWidget()
{
    if (requestElapsed_) delete requestElapsed_;
}

// ==================== UI 构建 ====================
void ChatPageWidget::setupUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========== 左侧：会话列表面板 ==========
    sidebarWidget_ = new QWidget(this);
    sidebarWidget_->setMinimumWidth(0);
    sidebarWidget_->setMaximumWidth(kSidebarExpandedWidth);
    sidebarWidget_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    sidebarWidget_->setStyleSheet(
        "QWidget {"
        "   background-color: rgba(245, 245, 247, 0.9);"
        "   border-right: 1px solid rgba(0,0,0,0.06);"
        "}"
        "QLabel { background: transparent; }"
        );
    sidebarOpacityEffect_ = new QGraphicsOpacityEffect(sidebarWidget_);
    sidebarOpacityEffect_->setOpacity(1.0);
    sidebarWidget_->setGraphicsEffect(sidebarOpacityEffect_);

    sidebarAnimation_ = new QPropertyAnimation(sidebarWidget_, "maximumWidth", this);
    sidebarAnimation_->setDuration(190);
    sidebarAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    sidebarOpacityAnimation_ = new QPropertyAnimation(sidebarOpacityEffect_, "opacity", this);
    sidebarOpacityAnimation_->setDuration(150);
    sidebarOpacityAnimation_->setEasingCurve(QEasingCurve::OutCubic);

    QVBoxLayout *sidebarLayout = new QVBoxLayout(sidebarWidget_);
    sidebarLayout->setContentsMargins(12, 12, 12, 12);
    sidebarLayout->setSpacing(10);

    // 顶部标题
    QHBoxLayout *topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);

    ElaText *historyLabel = new ElaText("历史记录", sidebarWidget_);
    historyLabel->setTextStyle(ElaTextType::Body);
    historyLabel->setTextPixelSize(15);
    historyLabel->setStyleSheet("color: #1a1a1a;");

    QFont sideTitleFont = historyLabel->font();
    sideTitleFont.setBold(true);
    historyLabel->setFont(sideTitleFont);
    historyLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    topBar->addWidget(historyLabel);
    topBar->addStretch();
    sidebarLayout->addLayout(topBar);

    // 新对话按钮
    ElaPushButton *newChatBtn = new ElaPushButton("+ 新对话", sidebarWidget_);
    newChatBtn->setFixedHeight(40);
    newChatBtn->setBorderRadius(8);
    connect(newChatBtn, &ElaPushButton::clicked, this, &ChatPageWidget::newConversationClicked);
    sidebarLayout->addWidget(newChatBtn);

    ElaText *recentLabel = new ElaText("最近对话", sidebarWidget_);
    recentLabel->setTextStyle(ElaTextType::Caption);
    recentLabel->setStyleSheet("color: #777; background: transparent;");
    sidebarLayout->addWidget(recentLabel);

    // 会话列表
    historyList_ = new QListWidget(sidebarWidget_);
    historyList_->setFrameShape(QFrame::NoFrame);
    historyList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    historyList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    historyList_->setTextElideMode(Qt::ElideRight);
    historyList_->setStyleSheet(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 8px 12px; border-radius: 6px; }"
        "QListWidget::item:selected { background-color: rgba(0,120,212,0.2); }"
        "QListWidget::item:hover { background-color: rgba(0,0,0,0.05); }"
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: rgba(0,0,0,0.18); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(0,0,0,0.32); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        );
    historyList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(historyList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item || !(item->flags() & Qt::ItemIsSelectable)) return;
        QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty()) emit conversationClicked(id);
    });
    connect(historyList_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = historyList_->itemAt(pos);
        if (!item || !(item->flags() & Qt::ItemIsSelectable)) return;
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;

        QMenu menu(historyList_);
        QAction *deleteAction = menu.addAction("删除此会话");
        QAction *renameAction = menu.addAction("重命名");

        QAction *chosen = menu.exec(historyList_->mapToGlobal(pos));
        if (chosen == deleteAction) {
            emit conversationDeleteRequested(id);
        } else if (chosen == renameAction) {
            bool ok;
            QString newTitle = QInputDialog::getText(this, "重命名", "请输入新标题:",
                                                      QLineEdit::Normal, item->text(), &ok);
            if (ok && !newTitle.isEmpty()) {
                emit conversationRenameRequested(id, newTitle);
            }
        }
    });
    sidebarLayout->addWidget(historyList_, 1);

    // 空状态标签
    historyEmptyLabel_ = new QLabel("暂无对话\n点击「+ 新对话」开始", sidebarWidget_);
    historyEmptyLabel_->setAlignment(Qt::AlignCenter);
    historyEmptyLabel_->setStyleSheet("color: #888; font-size: 14px;");
    historyEmptyLabel_->setVisible(false);
    sidebarLayout->addWidget(historyEmptyLabel_);

    mainLayout->addWidget(sidebarWidget_);

    // ========== 右侧：聊天区域 ==========
    QWidget *chatArea = new QWidget(this);
    QVBoxLayout *chatLayout = new QVBoxLayout(chatArea);
    chatLayout->setContentsMargins(20, 20, 20, 20);
    chatLayout->setSpacing(12);

    // 消息滚动区
    messageContainer_ = new QWidget();
    messageLayout_ = new QVBoxLayout(messageContainer_);
    messageLayout_->setContentsMargins(0, 0, 0, 0);
    messageLayout_->setSpacing(10);
    messageLayout_->addStretch();

    messageScrollArea_ = new ElaScrollArea(chatArea);
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
    chatLayout->addWidget(messageScrollArea_, 1);

    // 输入区域
    QHBoxLayout *inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(10);
    inputEdit_ = new ElaPlainTextEdit(chatArea);
    inputEdit_->setPlaceholderText("输入消息，Ctrl+Enter 发送...");
    inputEdit_->setFixedHeight(72);
    inputEdit_->installEventFilter(this);
    inputLayout->addWidget(inputEdit_, 1);

    sendButton_ = new ElaIconButton(ElaIconType::PaperPlane, 18, 40, 40, chatArea);
    sendButton_->setToolTip("发送 (Ctrl+Enter)");
    connect(sendButton_, &ElaIconButton::clicked, this, [this]() {
        emit sendClicked(inputEdit_->toPlainText().trimmed());
    });
    inputLayout->addWidget(sendButton_, 0, Qt::AlignBottom);
    chatLayout->addLayout(inputLayout);

    mainLayout->addWidget(chatArea);
}

// ==================== 时间感知问候 ====================
QString ChatPageWidget::timeBasedGreeting() const
{
    const int hour = QTime::currentTime().hour();
    QString greeting;
    if (hour >= 5 && hour < 9) {
        greeting = "早上好";
    } else if (hour >= 9 && hour < 12) {
        greeting = "上午好";
    } else if (hour >= 12 && hour < 14) {
        greeting = "中午好";
    } else if (hour >= 14 && hour < 18) {
        greeting = "下午好";
    } else {
        greeting = "晚上好";
    }
    return greeting + "，指挥官。";
}

// ==================== 添加消息气泡 ====================
void ChatPageWidget::appendMessage(const QString &text, bool isUser, bool showStepIndicator)
{
    MessageBubbleWidget *bubble = new MessageBubbleWidget(isUser, messageContainer_);
    bubble->setTimestamp(QDateTime::currentDateTime().toString("HH:mm"));

    if (isUser) {
        bubble->setUserContent(text);
    } else {
        if (showStepIndicator) {
            bubble->enableStepIndicator(true);
            if (!spinnerTimer_) {
                spinnerTimer_ = new QTimer(this);
                connect(spinnerTimer_, &QTimer::timeout, this, &ChatPageWidget::onSpinnerTick);
            }
            spinnerFrame_ = 0;
            spinnerTimer_->start(90);
            if (!requestElapsed_) requestElapsed_ = new QElapsedTimer();
            requestElapsed_->start();
        }
        currentAiBubble_ = bubble;
        currentAiBuffer_.clear();

        if (!text.isEmpty()) {
            bubble->setAiContent(text);
        }
    }

    messageLayout_->insertWidget(messageLayout_->count() - 1, bubble);

    QScrollBar *vBar = messageScrollArea_->verticalScrollBar();
    QMetaObject::invokeMethod(this, [vBar]() {
        vBar->setValue(vBar->maximum());
    }, Qt::QueuedConnection);
}

#if 0
// 旧版 appendMessage（使用 MessageBubbleWidget 替代）
void ChatPageWidget::appendMessage_OLD(const QString &text, bool isUser, bool showStepIndicator)
{
    QWidget *row = new QWidget(messageContainer_);
    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 4, 0, 4);
    rowLayout->setSpacing(8);

    static const QString kAvatarDir = QStringLiteral("C:/Users/ASUS/Desktop/practice/agent_/avatar/");
    QLabel *avatar = new QLabel(row);
    avatar->setFixedSize(36, 36);
    avatar->setAlignment(Qt::AlignCenter);
    {
        const QString avatarFile = kAvatarDir + (isUser ? "user.png" : "bot.png");
        if (QFile::exists(avatarFile)) {
            avatar->setStyleSheet(QStringLiteral(
                "QLabel {"
                "  border-image: url(%1) 0 0 0 0 stretch stretch;"
                "  border-radius: 18px;"
                "}"
            ).arg(avatarFile));
        } else {
            avatar->setText(isUser ? "U" : "E");
            avatar->setStyleSheet(QStringLiteral(
                "background-color: %1; color: white;"
                "border-radius: 18px; font-weight: bold; font-size: 15px;"
            ).arg(isUser ? "#4a9eff" : "#7c4dff"));
        }
    }

    ElaScrollPageArea *bubble = new ElaScrollPageArea(row);
    bubble->setBorderRadius(10);
    bubble->setMinimumHeight(0);
    bubble->setMaximumHeight(QWIDGETSIZE_MAX);
    bubble->setMaximumWidth(420);

    QVBoxLayout *bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 6, 12, 6);
    bubbleLayout->setSpacing(3);

    if (isUser) {
        ElaText *content = new ElaText(text, bubble);
        content->setTextStyle(ElaTextType::Body);
        content->setWordWrap(true);
        bubbleLayout->addWidget(content);
    } else {
        if (showStepIndicator) {
            QWidget *stepRow = new QWidget(bubble);
            QHBoxLayout *stepRowLayout = new QHBoxLayout(stepRow);
            stepRowLayout->setContentsMargins(0, 0, 0, 2);
            stepRowLayout->setSpacing(6);

            QLabel *stepIcon = new QLabel(stepRow);
            stepIcon->setStyleSheet("color:#4a9eff; font-size:13px; background:transparent;");
            stepIcon->setFixedWidth(16);
            QLabel *stepText = new QLabel("正在连接 DeepSeek...", stepRow);
            stepText->setStyleSheet("color:#888; font-size:12px; background:transparent;");

            stepRowLayout->addWidget(stepIcon);
            stepRowLayout->addWidget(stepText, 1);
            bubbleLayout->addWidget(stepRow);

            currentStepRow_ = stepRow;
            currentStepIcon_ = stepIcon;
            currentStepText_ = stepText;

            if (!spinnerTimer_) {
                spinnerTimer_ = new QTimer(this);
                connect(spinnerTimer_, &QTimer::timeout, this, [this]() {
                    static const QStringList frames = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
                    if (currentStepIcon_) {
                        currentStepIcon_->setText(frames[spinnerFrame_ % frames.size()]);
                    }
                    spinnerFrame_++;
                });
            }
            spinnerFrame_ = 0;
            spinnerTimer_->start(90);
            if (!requestElapsed_) requestElapsed_ = new QElapsedTimer();
            requestElapsed_->start();
        }

        QTextBrowser *content = new QTextBrowser(bubble);
        content->setMinimumHeight(0);
        content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        content->setReadOnly(true);
        content->setFrameShape(QFrame::NoFrame);
        content->setOpenLinks(false);
        content->setOpenExternalLinks(false);
        content->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        content->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        content->setStyleSheet(
            "QTextBrowser { background: transparent; border: none; }"
            "QTextBrowser a { color: #4a9eff; }"
            );
        content->setMinimumHeight(0);

        connect(content, &QTextBrowser::anchorClicked, this, [content](const QUrl &url) {
            const QString link = url.toString();
            if (link.startsWith("copycode:")) {
                bool ok = false;
                int idx = link.mid(9).toInt(&ok);
                const QStringList blocks = content->property("codeBlocks").toStringList();
                if (ok && idx >= 0 && idx < blocks.size()) {
                    QGuiApplication::clipboard()->setText(blocks.at(idx));
                    ElaMessageBar::success(ElaMessageBarType::TopRight, "已复制", "代码已复制到剪贴板", 1500);
                }
            } else {
                QDesktopServices::openUrl(url);
            }
        });

        QStringList codeBlocks;
        content->setHtml(markdownToHtml(text, &codeBlocks));
        content->setProperty("codeBlocks", codeBlocks);
        adjustTextBrowserHeight(content);
        bubbleLayout->addWidget(content);

        currentAiContent_ = content;
    }

    QLabel *timeLabel = new QLabel(QDateTime::currentDateTime().toString("HH:mm"), bubble);
    timeLabel->setStyleSheet("font-size: 11px; color: #999; background: transparent;");
    timeLabel->setAlignment(isUser ? Qt::AlignRight : Qt::AlignLeft);
    bubbleLayout->addWidget(timeLabel);

    if (isUser) {
        rowLayout->addStretch();
        rowLayout->addWidget(bubble, 0, Qt::AlignTop);
        rowLayout->addWidget(avatar, 0, Qt::AlignTop);
    } else {
        rowLayout->addWidget(avatar, 0, Qt::AlignTop);
        rowLayout->addWidget(bubble, 0, Qt::AlignTop);
        rowLayout->addStretch();
    }

    messageLayout_->insertWidget(messageLayout_->count() - 1, row);

    QScrollBar *vBar = messageScrollArea_->verticalScrollBar();
    QMetaObject::invokeMethod(this, [vBar]() {
        vBar->setValue(vBar->maximum());
    }, Qt::QueuedConnection);
}
#endif

// ==================== 聊天背景 ====================
void ChatPageWidget::applyChatBg(int opacityPercent)
{
    currentBgOpacity_ = opacityPercent;

    if (bgPixmap_.isNull() || !messageScrollArea_) return;

    const qreal opacity = qBound(0.0, opacityPercent / 100.0 * 0.85, 0.85);
    const QSize viewportSize = messageScrollArea_->viewport()->size();
    if (viewportSize.isEmpty()) return;

    QPixmap scaledBg = bgPixmap_.scaled(viewportSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap result(viewportSize);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(opacity);
    const int x = (viewportSize.width() - scaledBg.width()) / 2;
    const int y = (viewportSize.height() - scaledBg.height()) / 2;
    p.drawPixmap(x, y, scaledBg);
    p.end();

    QPalette pal = messageScrollArea_->viewport()->palette();
    pal.setBrush(QPalette::Window, QBrush(result));
    messageScrollArea_->viewport()->setPalette(pal);
    messageScrollArea_->viewport()->setAutoFillBackground(true);
    messageScrollArea_->viewport()->update();
}

void ChatPageWidget::setBackgroundPixmap(const QPixmap &pixmap)
{
    bgPixmap_ = pixmap;
}

// ==================== 侧边栏 ====================
void ChatPageWidget::toggleSidebar()
{
    if (!sidebarWidget_) return;
    sidebarCollapsed_ = !sidebarCollapsed_;

    const bool collapsing = sidebarCollapsed_;
    const int startWidth = sidebarWidget_->isVisible()
                               ? sidebarWidget_->maximumWidth()
                               : 0;
    const int endWidth = collapsing ? 0 : kSidebarExpandedWidth;

    if (sidebarAnimation_) sidebarAnimation_->stop();
    if (sidebarOpacityAnimation_) sidebarOpacityAnimation_->stop();

    if (!collapsing) {
        sidebarWidget_->setVisible(true);
        sidebarWidget_->setMaximumWidth(qMax(1, startWidth));
        if (sidebarOpacityEffect_) sidebarOpacityEffect_->setOpacity(0.0);
    }

    emit sidebarCollapsedChanged(sidebarCollapsed_);

    if (sidebarAnimation_) {
        sidebarAnimation_->disconnect();
        sidebarAnimation_->setStartValue(startWidth);
        sidebarAnimation_->setEndValue(endWidth);
        connect(sidebarAnimation_, &QPropertyAnimation::finished, this, [this, collapsing]() {
            if (collapsing) {
                sidebarWidget_->setVisible(false);
            }
            sidebarWidget_->setMinimumWidth(0);
            sidebarWidget_->setMaximumWidth(collapsing ? 0 : kSidebarExpandedWidth);
        });
        sidebarAnimation_->start();
    }

    if (sidebarOpacityAnimation_) {
        sidebarOpacityAnimation_->setStartValue(collapsing ? 1.0 : 0.0);
        sidebarOpacityAnimation_->setEndValue(collapsing ? 0.0 : 1.0);
        sidebarOpacityAnimation_->start();
    }
}

void ChatPageWidget::restoreSidebarState(bool collapsed)
{
    if (!sidebarWidget_) return;

    sidebarCollapsed_ = collapsed;
    if (collapsed) {
        sidebarWidget_->setVisible(false);
        sidebarWidget_->setMaximumWidth(0);
        if (sidebarOpacityEffect_) sidebarOpacityEffect_->setOpacity(0.0);
    } else {
        sidebarWidget_->setVisible(true);
        sidebarWidget_->setMaximumWidth(kSidebarExpandedWidth);
        if (sidebarOpacityEffect_) sidebarOpacityEffect_->setOpacity(1.0);
    }
    emit sidebarCollapsedChanged(collapsed);
}

// ==================== 会话列表 ====================
void ChatPageWidget::refreshConversationList(const QJsonArray &meta, const QString &currentId)
{
    currentConversationId_ = currentId;
    historyList_->clear();
    if (meta.isEmpty()) {
        historyList_->setVisible(false);
        historyEmptyLabel_->setVisible(true);
        return;
    }
    historyList_->setVisible(true);
    historyEmptyLabel_->setVisible(false);

    for (const QJsonValue &val : meta) {
        QJsonObject obj = val.toObject();
        QString title = obj["title"].toString();
        QString id = obj["id"].toString();
        QListWidgetItem *item = new QListWidgetItem(title, historyList_);
        item->setData(Qt::UserRole, id);
        item->setToolTip(title);
        if (id == currentConversationId_) {
            item->setSelected(true);
        }
    }
}

void ChatPageWidget::setCurrentConversationId(const QString &id)
{
    currentConversationId_ = id;
}

// ==================== 清空聊天显示 ====================
void ChatPageWidget::clearChatDisplay()
{
    currentAiBubble_ = nullptr;
    while (messageLayout_->count() > 1) {
        QLayoutItem *item = messageLayout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

// ==================== 恢复对话 ====================
void ChatPageWidget::restoreConversation(const QJsonArray &messages)
{
    clearChatDisplay();
    for (const QJsonValue &val : messages) {
        QJsonObject msg = val.toObject();
        QString role = msg["role"].toString();
        if (role == "tool") continue;
        if (role == "assistant" && msg.contains("tool_calls")) continue;
        QString content = msg["content"].toString();
        if (content.isEmpty()) continue;
        appendMessage(content, role == "user");
    }
}

// ==================== 显示问候语 ====================
void ChatPageWidget::showGreeting()
{
    clearChatDisplay();
    appendMessage(timeBasedGreeting(), false);
}

// ==================== 流式响应回调 ====================
void ChatPageWidget::onChunkReceived(const QString &delta)
{
    if (!currentAiBubble_) return;
    currentAiBuffer_ += delta;
    if (currentAiBuffer_.length() == delta.length()) {
        currentAiBubble_->updateStep("正在生成回复...");
    }
    // 节流：50ms 内多个 token 只触发一次 UI 刷新
    if (!throttleTimer_->isActive()) {
        throttleTimer_->start();
    }
}

void ChatPageWidget::flushAiContent()
{
    if (!currentAiBubble_) return;
    currentAiBubble_->setAiStreamingContent(currentAiBuffer_);
    QScrollBar *vBar = messageScrollArea_->verticalScrollBar();
    vBar->setValue(vBar->maximum());
}

void ChatPageWidget::onResponseCompleted(const QString &fullText)
{
    if (!currentAiBubble_) return;

    // 先刷出缓冲区中的剩余内容
    if (throttleTimer_->isActive()) {
        throttleTimer_->stop();
        flushAiContent();
    }

    double elapsedSec = requestElapsed_ ? requestElapsed_->elapsed() / 1000.0 : 0.0;
    finishAiStep(true, QString("✓ 完成 · 用时 %1s").arg(QString::number(elapsedSec, 'f', 1)));

    if (!fullText.isEmpty()) {
        currentAiBubble_->setAiContent(fullText);
    }
}

void ChatPageWidget::onResponseError(const QString &errorMessage)
{
    if (!currentAiBubble_) return;
    if (throttleTimer_->isActive()) {
        throttleTimer_->stop();
    }
    finishAiStep(false, "✗ 请求失败 · " + errorMessage.left(50));
    currentAiBubble_->setAiStreamingContent("请求失败: " + errorMessage);
}

// ==================== 步骤指示器 ====================
void ChatPageWidget::onSpinnerTick()
{
    if (currentAiBubble_) {
        currentAiBubble_->spinnerTick(spinnerFrame_);
    }
    spinnerFrame_++;
}

void ChatPageWidget::updateAiStep(const QString &text)
{
    if (currentAiBubble_) currentAiBubble_->updateStep(text);
}

void ChatPageWidget::finishAiStep(bool success, const QString &finalText)
{
    if (!currentAiBubble_) return;
    if (spinnerTimer_) spinnerTimer_->stop();
    currentAiBubble_->finishStep(success, finalText);
}

// ==================== 输入控制 ====================
void ChatPageWidget::setInputEnabled(bool enabled)
{
    inputEdit_->setEnabled(enabled);
    sendButton_->setEnabled(enabled);
    if (enabled) inputEdit_->setFocus();
}

void ChatPageWidget::clearAiState()
{
    currentAiBubble_ = nullptr;
    if (spinnerTimer_) spinnerTimer_->stop();
}

// ==================== 事件过滤 ====================
bool ChatPageWidget::eventFilter(QObject *watched, QEvent *event)
{
    // Ctrl+Enter 发送
    if (watched == inputEdit_ && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && (keyEvent->modifiers() & Qt::ControlModifier)) {
            emit sendClicked(inputEdit_->toPlainText().trimmed());
            return true;
        }
    }

    // 聊天视口大小变化时重新适配背景
    if (watched == (messageScrollArea_ ? messageScrollArea_->viewport() : nullptr)
        && event->type() == QEvent::Resize) {
        applyChatBg(currentBgOpacity_);
        return false;
    }

    return QWidget::eventFilter(watched, event);
}
