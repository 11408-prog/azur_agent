#include "chat/chatpagewidget.h"
#include "ui/markdownrenderer.h"
#include "ui/messagebubblewidget.h"
#include "ui/uiconstants.h"
#include "ui/conversationview.h"
#include "data/appsettings.h"

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
    , spinnerTimer_(nullptr)
    , spinnerFrame_(0)
    , requestElapsed_(nullptr)
    , currentBgOpacity_(25)
{
    setupUI();

    connect(conversationView_, &ConversationView::sendRequested, this, [this](const QString &text) {
        conversationView_->clearInput();
        emit sendClicked(text);
    });
    connect(conversationView_, &ConversationView::cancelRequested,
            this, &ChatPageWidget::cancelRequested);
    connect(conversationView_, &ConversationView::viewportResized, this, [this]() {
        applyChatBg(currentBgOpacity_);
    });
    connect(conversationView_, &ConversationView::firstChunkOfResponse, this, [this]() {
        if (MessageBubbleWidget *bubble = conversationView_->currentAiBubble()) {
            bubble->updateStep("正在生成回复...");
        }
    });
}

ChatPageWidget::~ChatPageWidget()
{
    if (requestElapsed_) delete requestElapsed_;
}

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
    // MODIFIED: 侧边栏改为半透明暖白毛玻璃
    sidebarWidget_->setStyleSheet(
        "QWidget {"
        "   background-color: rgba(247, 249, 252, 0.88);"
        "   border-right: 1px solid rgba(150, 170, 200, 0.25);"
        "}"
        "QLabel { background: transparent; color: #3a3a4a; }"
        "QListWidget { background: transparent; border: none; color: #4a4a5a; }"
        "QListWidget::item { padding: 8px 12px; border-radius: 8px; }"
        "QListWidget::item:selected {"
        "   background-color: rgba(15, 95, 240, 0.18);"
        "   color: #2a2a3a;"
        "}"
        "QListWidget::item:hover { background-color: rgba(150, 170, 200, 0.15); }"
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: rgba(0,0,0,0.18); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(0,0,0,0.32); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
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

    QHBoxLayout *topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);

    ElaText *historyLabel = new ElaText("历史记录", sidebarWidget_);
    historyLabel->setTextStyle(ElaTextType::Body);
    historyLabel->setTextPixelSize(15);
    // MODIFIED: 标题颜色改为暖深棕
    historyLabel->setStyleSheet("color: #2a2a3a;");

    QFont sideTitleFont = historyLabel->font();
    sideTitleFont.setBold(true);
    historyLabel->setFont(sideTitleFont);
    historyLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    topBar->addWidget(historyLabel);
    topBar->addStretch();
    sidebarLayout->addLayout(topBar);

    ElaPushButton *newChatBtn = new ElaPushButton("+ 新对话", sidebarWidget_);
    newChatBtn->setFixedHeight(40);
    newChatBtn->setBorderRadius(8);
    connect(newChatBtn, &ElaPushButton::clicked, this, &ChatPageWidget::newConversationClicked);
    sidebarLayout->addWidget(newChatBtn);

    ElaText *recentLabel = new ElaText("最近对话", sidebarWidget_);
    recentLabel->setTextStyle(ElaTextType::Caption);
    // MODIFIED: 副标题颜色改为暖灰
    recentLabel->setStyleSheet("color: #8a8a9a; background: transparent;");
    sidebarLayout->addWidget(recentLabel);

    historyList_ = new QListWidget(sidebarWidget_);
    historyList_->setFrameShape(QFrame::NoFrame);
    historyList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    historyList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    historyList_->setTextElideMode(Qt::ElideRight);
    historyList_->setStyleSheet(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 8px 12px; border-radius: 6px; }"
        "QListWidget::item:selected { background-color: rgba(15, 95, 240, 0.18); }"
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

    historyEmptyLabel_ = new QLabel("暂无对话\n点击「+ 新对话」开始", sidebarWidget_);
    historyEmptyLabel_->setAlignment(Qt::AlignCenter);
    historyEmptyLabel_->setStyleSheet("color: #888; font-size: 14px;");
    historyEmptyLabel_->setVisible(false);
    sidebarLayout->addWidget(historyEmptyLabel_);

    mainLayout->addWidget(sidebarWidget_);

    // ========== 右侧：聊天区域 ==========
    QWidget *chatArea = new QWidget(this);
    QVBoxLayout *chatLayout = new QVBoxLayout(chatArea);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

    conversationView_ = new ConversationView(chatArea);
    chatLayout->addWidget(conversationView_, 1);

    mainLayout->addWidget(chatArea);

    conversationView_->setStatusBarVisible(AppSettings::showStatusBar());
    conversationView_->setStatusBarModelName(AppSettings::model());
}

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

void ChatPageWidget::appendMessage(const QString &text, bool isUser, bool showStepIndicator,
                                    const QDateTime &timestamp)
{
    MessageBubbleWidget *bubble = conversationView_->appendMessage(text, isUser, timestamp);

    if (!isUser && showStepIndicator) {
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
}

void ChatPageWidget::clearChatDisplay()
{
    conversationView_->clearChatDisplay();
}

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

        // 消息本身带的时间戳（新消息创建时写入的，老消息由 ConversationManager
        // 在读取时统一回填过，理论上到这里都应该是有效值）。这里再兜底一次
        // fromString 解析失败的极端情况，保证不会崩，而不是这个字段本身缺失的常规路径。
        QDateTime ts = QDateTime::fromString(msg["timestamp"].toString(), Qt::ISODate);
        appendMessage(content, role == "user", false, ts);
    }
}

void ChatPageWidget::showGreeting()
{
    clearChatDisplay();
    appendMessage(timeBasedGreeting(), false);
}

void ChatPageWidget::onChunkReceived(const QString &delta)
{
    conversationView_->onChunkReceived(delta);
}

void ChatPageWidget::onResponseCompleted(const QString &fullText)
{
    MessageBubbleWidget *bubble = conversationView_->currentAiBubble();
    if (!bubble) return;

    conversationView_->flushPendingContent();

    double elapsedSec = requestElapsed_ ? requestElapsed_->elapsed() / 1000.0 : 0.0;
    finishAiStep(true, QString("✓ 完成 · 用时 %1s").arg(QString::number(elapsedSec, 'f', 1)));

    if (!fullText.isEmpty()) {
        bubble->setAiContent(fullText);
    }
}

void ChatPageWidget::onResponseError(const QString &errorMessage)
{
    MessageBubbleWidget *bubble = conversationView_->currentAiBubble();
    if (!bubble) return;
    conversationView_->flushPendingContent();
    finishAiStep(false, "✗ 请求失败 · " + errorMessage.left(50));
    bubble->setAiStreamingContent("请求失败: " + errorMessage);
}

void ChatPageWidget::onSpinnerTick()
{
    if (MessageBubbleWidget *bubble = conversationView_->currentAiBubble()) {
        bubble->spinnerTick(spinnerFrame_);
    }
    spinnerFrame_++;
}

void ChatPageWidget::updateAiStep(const QString &text)
{
    if (MessageBubbleWidget *bubble = conversationView_->currentAiBubble()) {
        bubble->updateStep(text);
    }
}

void ChatPageWidget::finishAiStep(bool success, const QString &finalText)
{
    MessageBubbleWidget *bubble = conversationView_->currentAiBubble();
    if (!bubble) return;
    if (spinnerTimer_) spinnerTimer_->stop();
    bubble->finishStep(success, finalText);
}

void ChatPageWidget::applyChatBg(int opacityPercent)
{
    currentBgOpacity_ = opacityPercent;

    ElaScrollArea *scrollArea = conversationView_->scrollArea();
    if (bgPixmap_.isNull() || !scrollArea) return;

    const qreal opacity = qBound(0.0, opacityPercent / 100.0 * 0.85, 0.85);
    const QSize viewportSize = scrollArea->viewport()->size();
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

    QPalette pal = scrollArea->viewport()->palette();
    pal.setBrush(QPalette::Window, QBrush(result));
    scrollArea->viewport()->setPalette(pal);
    scrollArea->viewport()->setAutoFillBackground(true);
    scrollArea->viewport()->update();
}

void ChatPageWidget::setBackgroundPixmap(const QPixmap &pixmap)
{
    bgPixmap_ = pixmap;
}

void ChatPageWidget::clearChatBg()
{
    bgPixmap_ = QPixmap();
    ElaScrollArea *scrollArea = conversationView_->scrollArea();
    if (!scrollArea) return;
    QPalette pal = scrollArea->viewport()->palette();
    pal.setBrush(QPalette::Window, QBrush());
    scrollArea->viewport()->setPalette(pal);
    scrollArea->viewport()->setAutoFillBackground(false);
    scrollArea->viewport()->update();
}

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

void ChatPageWidget::setInputEnabled(bool enabled)
{
    conversationView_->setInputEnabled(enabled);
}

void ChatPageWidget::clearAiState()
{
    conversationView_->clearCurrentAiBubble();
    if (spinnerTimer_) spinnerTimer_->stop();
}

void ChatPageWidget::cancelAiResponse()
{
    MessageBubbleWidget *bubble = conversationView_->currentAiBubble();
    if (bubble) {
        conversationView_->flushPendingContent();
        finishAiStep(false, "✗ 已取消生成");
        bubble->setAiStreamingContent(QStringLiteral("（已取消生成）"));
    }
    clearAiState();
}
