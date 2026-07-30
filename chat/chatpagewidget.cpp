#include "chat/chatpagewidget.h"
#include "ui/markdownrenderer.h"
#include "ui/messagebubblewidget.h"
#include "ui/uiconstants.h"
#include "ui/conversationview.h"

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

    // ---- 转发 ConversationView 的信号 ----
    // 发送/回车 -> 先清空输入框，再对外仍然发出 sendClicked(text)（保持原有对外接口不变）。
    // 这里之前是直接把 sendRequested 转发成 sendClicked 的信号到信号连接，
    // 漏掉了清空输入框这一步——不管是点发送按钮还是按 Enter，发送后文本都会一直留在
    // 输入框里（Project 模式的 onSendRequested 里有调用 clearInput()，Chat 模式这边漏掉了）。
    connect(conversationView_, &ConversationView::sendRequested, this, [this](const QString &text) {
        conversationView_->clearInput();
        emit sendClicked(text);
    });
    // 停止按钮 -> 对外仍然是 cancelRequested()
    connect(conversationView_, &ConversationView::cancelRequested,
            this, &ChatPageWidget::cancelRequested);
    // 消息区视口尺寸变化 -> 重新适配聊天背景图（原来在 eventFilter 里处理）
    connect(conversationView_, &ConversationView::viewportResized, this, [this]() {
        applyChatBg(currentBgOpacity_);
    });
    // 收到本轮回复第一个流式片段 -> 更新步骤指示器文案
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

    // ========== 右侧：聊天区域（消息展示 + 输入框，交给共享的 ConversationView） ==========
    QWidget *chatArea = new QWidget(this);
    QVBoxLayout *chatLayout = new QVBoxLayout(chatArea);
    chatLayout->setContentsMargins(20, 20, 20, 20);
    chatLayout->setSpacing(12);

    conversationView_ = new ConversationView(chatArea);
    chatLayout->addWidget(conversationView_, 1);

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
    MessageBubbleWidget *bubble = conversationView_->appendMessage(text, isUser);

    // 步骤指示器是 Chat 模式独有的东西（Project 模式用外部 ActivityPanel），
    // ConversationView 不知道这件事，这里拿到气泡指针后自己叠加。
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

// ==================== 清空聊天显示 ====================
void ChatPageWidget::clearChatDisplay()
{
    conversationView_->clearChatDisplay();
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
    conversationView_->onChunkReceived(delta);
}

void ChatPageWidget::onResponseCompleted(const QString &fullText)
{
    MessageBubbleWidget *bubble = conversationView_->currentAiBubble();
    if (!bubble) return;

    // 先刷出缓冲区中的剩余内容
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

// ==================== 步骤指示器 ====================
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

// ==================== 聊天背景 ====================
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

// ==================== 输入控制 ====================
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
        // 先把节流缓冲区里已经收到、但还没来得及显示的内容刷出来，
        // 免得取消瞬间还有几个字没上屏
        conversationView_->flushPendingContent();
        finishAiStep(false, "✗ 已取消生成");
        // 如果还没收到任何流式内容，气泡这时候还停留在"思考中 ⠋"的占位动画上，
        // 必须显式给它设置一段明确的文字，否则 setAiStreamingContent 不会被调用，
        // 动画会一直转下去，界面上就会永远卡在"思考中"，这正是用户看到的现象。
        bubble->setAiStreamingContent(QStringLiteral("（已取消生成）"));
    }
    clearAiState();
}
