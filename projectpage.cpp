#include "projectpage.h"
#include "activitypanel.h"
#include "tool_executor.h"
#include "projectsession.h"
#include "markdownrenderer.h"
#include "messagebubblewidget.h"
#include "promptloader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeView>
#include <QFileSystemModel>
#include <QTextBrowser>
#include <QScrollBar>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QSettings>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <cmath>

#include <QKeyEvent>

#include <QPainter>
#include <QPainterPath>
#include <QLabel>

#include <ElaScrollArea.h>
#include <ElaPlainTextEdit.h>
#include <ElaIconButton.h>
#include <ElaText.h>
#include <ElaIcon.h>
#include <ElaScrollPageArea.h>
#include <ElaMessageBar.h>
#include <ElaContentDialog.h>

// ==================== 构造 / 析构 ====================
ProjectPage::ProjectPage(AgentEngine *engine, QWidget *parent)
    : QWidget(parent)
    , splitter_(nullptr)
    , leftPanel_(nullptr)
    , fileTree_(nullptr)
    , fsModel_(nullptr)
    , leftToggleBtn_(nullptr)
    , rightToggleBtn_(nullptr)
    , messageScrollArea_(nullptr)
    , messageContainer_(nullptr)
    , messageLayout_(nullptr)
    , inputEdit_(nullptr)
    , sendButton_(nullptr)
    , rightPanel_(nullptr)
    , activityPanel_(nullptr)
    , engine_(nullptr)
{
    engine_ = engine;

    setupUI();
    restorePanelCollapseState();

    // 创建 AgentEngine，接管 DeepSeekClient 的信号路由
    connect(engine_, &AgentEngine::chunkReceived, this, &ProjectPage::onChunkReceived);
    connect(engine_, &AgentEngine::finished, this, &ProjectPage::onResponseCompleted);
    connect(engine_, &AgentEngine::errorOccurred, this, &ProjectPage::onError);

    // 写操作确认弹窗
    connect(engine_, &AgentEngine::writeConfirmationRequired,
            this, [this](const QStringList &diffList) {
        ElaContentDialog dlg(this);
        dlg.setWindowTitle("修改确认");

        QWidget *centralWidget = new QWidget(&dlg);
        QVBoxLayout *centralLayout = new QVBoxLayout(centralWidget);
        centralLayout->setContentsMargins(0, 0, 0, 0);
        centralLayout->setSpacing(10);

        ElaText *infoLabel = new ElaText(
            QString("AI 请求对以下 %1 个文件进行修改：").arg(diffList.size()), centralWidget);
        infoLabel->setTextStyle(ElaTextType::Body);
        QFont infoFont = infoLabel->font();
        infoFont.setBold(true);
        infoLabel->setFont(infoFont);
        centralLayout->addWidget(infoLabel);

        QTextBrowser *diffBrowser = new QTextBrowser(centralWidget);
        diffBrowser->setReadOnly(true);
        diffBrowser->setFrameShape(QFrame::NoFrame);
        diffBrowser->setStyleSheet(
            "QTextBrowser { background: #1e1e1e; color: #d4d4d4; "
            "font-family: 'Consolas', monospace; font-size: 13px; "
            "padding: 12px; border-radius: 8px; }");
        diffBrowser->setPlainText(diffList.join("\n\n--------------------\n\n"));
        centralLayout->addWidget(diffBrowser, 1);

        dlg.setCentralWidget(centralWidget);
        dlg.setLeftButtonText("拒绝修改");
        dlg.setRightButtonText("接受修改");

        connect(&dlg, &ElaContentDialog::leftButtonClicked, &dlg, &QDialog::reject);
        connect(&dlg, &ElaContentDialog::rightButtonClicked, &dlg, &QDialog::accept);

        const bool accepted = (dlg.exec() == QDialog::Accepted);
        engine_->confirmWrite(accepted);
    });

    // 引擎步骤更新输出到活动面板
    connect(engine_, &AgentEngine::stepChanged, this, [this](const QString &text) {
        activityPanel_->onStepChanged(text);
    });

    // 节流程式输出：每 50ms 刷新一次 UI
    throttleTimer_ = new QTimer(this);
    throttleTimer_->setSingleShot(true);
    throttleTimer_->setInterval(50);
    connect(throttleTimer_, &QTimer::timeout, this, &ProjectPage::flushAiContent);
}

ProjectPage::~ProjectPage() = default;

// ==================== 激活 / 停用 ====================
void ProjectPage::setActive(bool active)
{
    if (active == isActive_) return;

    if (!active) {
        // 停用时取消正在进行的请求
        engine_->cancel();
        isWaitingResponse_ = false;
        currentAiBubble_ = nullptr;
    }

    isActive_ = active;
}

// ==================== 事件过滤 ====================
bool ProjectPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == inputEdit_ && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            onSendClicked();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ProjectPage::setProjectPath(const QString &path)
{
    projectPath_ = path;
    if (fsModel_ && !path.isEmpty()) {
        fsModel_->setRootPath(path);
        fileTree_->setRootIndex(fsModel_->index(path));
    }

    // 项目路径变更时自动重建索引
    if (!path.isEmpty()) {
        rebuildIndex();
    }
}

// ==================== 项目索引 ====================
void ProjectPage::rebuildIndex()
{
    if (projectPath_.isEmpty()) return;

    qDebug() << "[PROJ_PAGE] 重建项目索引 | path=" << projectPath_;

    // 检查缓存是否仍然有效
    if (!ProjectAnalyzer::needsRebuild(projectPath_)) {
        projectIndex_ = ProjectAnalyzer::loadIndex(projectPath_);
        if (!projectIndex_.isEmpty()) {
            indexSummary_ = ProjectAnalyzer::generateSummary(projectIndex_);
            qDebug() << "[PROJ_PAGE] 使用缓存索引";
            return;
        }
    }

    // 重建索引
    projectIndex_ = ProjectAnalyzer::buildIndex(projectPath_);
    if (!projectIndex_.isEmpty()) {
        ProjectAnalyzer::saveIndex(projectPath_, projectIndex_);
        indexSummary_ = ProjectAnalyzer::generateSummary(projectIndex_);
        qDebug() << "[PROJ_PAGE] 索引已重建并保存";
    } else {
        indexSummary_.clear();
        qWarning() << "[PROJ_PAGE] 索引构建失败";
    }
}

void ProjectPage::loadSystemPrompt()
{
    systemPrompt_ = PromptLoader::buildSystemPrompt();

    // 如果索引摘要存在，追加到 system prompt 末尾
    if (!indexSummary_.isEmpty() && !indexSummary_.contains("（项目索引不可用）")) {
        systemPrompt_ += "\n\n" + indexSummary_;
    }
}

#if 0
// 以下函数已迁移到 PromptLoader
QString ProjectPage::loadPromptFile(const QString &filename) const { return {}; }
#endif

// ==================== UI 搭建 ====================
void ProjectPage::setupUI()
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setHandleWidth(4);
    splitter_->setChildrenCollapsible(false);
    splitter_->setStyleSheet(
        "QSplitter::handle { background: rgba(0,0,0,0.06); }"
        "QSplitter::handle:hover { background: rgba(0,120,212,0.25); }"
    );

    // ========== 左侧：文件树（可折叠，风格与对话页历史侧栏一致） ==========
    leftPanel_ = new QWidget();
    leftPanel_->setMinimumWidth(0);
    leftPanel_->setMaximumWidth(kLeftPanelWidth);
    leftPanel_->setStyleSheet(
        "QWidget {"
        "   background-color: rgba(245, 245, 247, 0.9);"
        "   border-right: 1px solid rgba(0,0,0,0.06);"
        "}"
        "QLabel { background: transparent; }"
    );
    leftPanelOpacityEffect_ = new QGraphicsOpacityEffect(leftPanel_);
    leftPanelOpacityEffect_->setOpacity(1.0);
    leftPanel_->setGraphicsEffect(leftPanelOpacityEffect_);

    leftPanelWidthAnim_ = new QPropertyAnimation(leftPanel_, "maximumWidth", this);
    leftPanelWidthAnim_->setDuration(190);
    leftPanelWidthAnim_->setEasingCurve(QEasingCurve::OutCubic);
    leftPanelOpacityAnim_ = new QPropertyAnimation(leftPanelOpacityEffect_, "opacity", this);
    leftPanelOpacityAnim_->setDuration(150);
    leftPanelOpacityAnim_->setEasingCurve(QEasingCurve::OutCubic);

    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel_);
    leftLayout->setContentsMargins(14, 14, 14, 14);
    leftLayout->setSpacing(10);

    ElaText *fileTitle = new ElaText("项目文件", leftPanel_);
    fileTitle->setTextStyle(ElaTextType::Body);
    fileTitle->setTextPixelSize(15);
    fileTitle->setStyleSheet("color: #1a1a1a;");
    QFont sideFont = fileTitle->font();
    sideFont.setBold(true);
    fileTitle->setFont(sideFont);
    leftLayout->addWidget(fileTitle);

    fsModel_ = new QFileSystemModel(this);
    fsModel_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    fsModel_->setNameFilterDisables(false);

    fileTree_ = new QTreeView(leftPanel_);
    fileTree_->setModel(fsModel_);
    fileTree_->setFrameShape(QFrame::NoFrame);
    fileTree_->setAnimated(true);
    fileTree_->setIndentation(16);
    fileTree_->setHeaderHidden(true);
    fileTree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    fileTree_->setStyleSheet(
        "QTreeView { background: transparent; border: none; }"
        "QTreeView::item { padding: 5px 6px; border-radius: 6px; }"
        "QTreeView::item:selected { background-color: rgba(0,120,212,0.2); }"
        "QTreeView::item:hover { background-color: rgba(0,0,0,0.05); }"
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: rgba(0,0,0,0.18); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(0,0,0,0.32); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
    );
    // 只显示文件名列
    for (int i = 1; i < fsModel_->columnCount(); ++i) {
        fileTree_->hideColumn(i);
    }
    leftLayout->addWidget(fileTree_, 1);

    splitter_->addWidget(leftPanel_);

    // ========== 中间：Agent对话 ==========
    QWidget *centerPanel = new QWidget();
    QVBoxLayout *centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(16, 14, 16, 14);
    centerLayout->setSpacing(10);

    // 顶部标题栏：左右两侧折叠按钮常驻此处，不随面板一起被隐藏
    QHBoxLayout *centerHeaderLayout = new QHBoxLayout();
    centerHeaderLayout->setContentsMargins(0, 0, 0, 0);
    centerHeaderLayout->setSpacing(8);

    leftToggleBtn_ = new ElaIconButton(ElaIconType::SidebarFlip, 16, 28, 28, centerPanel);
    leftToggleBtn_->setToolTip("隐藏项目文件");
    connect(leftToggleBtn_, &ElaIconButton::clicked, this, [this]() { togglePanel(true); });
    centerHeaderLayout->addWidget(leftToggleBtn_, 0, Qt::AlignVCenter);

    ElaText *chatTitle = new ElaText("Agent 对话", centerPanel);
    chatTitle->setTextStyle(ElaTextType::Body);
    chatTitle->setTextPixelSize(15);
    chatTitle->setStyleSheet("color: #1a1a1a;");
    chatTitle->setFont(sideFont);
    centerHeaderLayout->addWidget(chatTitle, 0, Qt::AlignVCenter);
    centerHeaderLayout->addStretch();

    rightToggleBtn_ = new ElaIconButton(ElaIconType::SidebarFlip, 16, 28, 28, centerPanel);
    rightToggleBtn_->setToolTip("隐藏 AI 活动");
    connect(rightToggleBtn_, &ElaIconButton::clicked, this, [this]() { togglePanel(false); });
    centerHeaderLayout->addWidget(rightToggleBtn_, 0, Qt::AlignVCenter);

    centerLayout->addLayout(centerHeaderLayout);

    messageContainer_ = new QWidget();
    messageLayout_ = new QVBoxLayout(messageContainer_);
    messageLayout_->setContentsMargins(0, 0, 0, 0);
    messageLayout_->setSpacing(10);
    messageLayout_->addStretch();

    messageScrollArea_ = new ElaScrollArea(centerPanel);
    messageScrollArea_->setWidget(messageContainer_);
    messageScrollArea_->setWidgetResizable(true);
    messageScrollArea_->setFrameShape(QFrame::NoFrame);
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
    centerLayout->addWidget(messageScrollArea_, 1);

    QHBoxLayout *inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(10);
    inputEdit_ = new ElaPlainTextEdit(centerPanel);
    inputEdit_->setPlaceholderText("输入消息，Enter 发送，Shift+Enter 换行...");
    inputEdit_->setFixedHeight(64);
    inputEdit_->installEventFilter(this);
    inputLayout->addWidget(inputEdit_, 1);

    sendButton_ = new ElaIconButton(ElaIconType::PaperPlane, 18, 36, 36, centerPanel);
    sendButton_->setToolTip("发送 (Enter)");
    connect(sendButton_, &ElaIconButton::clicked, this, &ProjectPage::onSendClicked);
    inputLayout->addWidget(sendButton_, 0, Qt::AlignBottom);

    stopButton_ = new ElaIconButton(ElaIconType::Ban, 18, 36, 36, centerPanel);
    stopButton_->setToolTip("停止生成");
    stopButton_->setVisible(false);
    connect(stopButton_, &ElaIconButton::clicked, this, [this]() {
        engine_->cancel();
        setInputEnabled(true);
    });
    inputLayout->addWidget(stopButton_, 0, Qt::AlignBottom);
    centerLayout->addLayout(inputLayout);

    splitter_->addWidget(centerPanel);

    // ========== 右侧：AI 活动面板（可折叠） ==========
    rightPanel_ = new QWidget();
    rightPanel_->setMinimumWidth(0);
    rightPanel_->setMaximumWidth(kRightPanelWidth);
    rightPanel_->setStyleSheet(
        "QWidget {"
        "   background-color: rgba(245, 245, 247, 0.9);"
        "   border-left: 1px solid rgba(0,0,0,0.06);"
        "}"
        "QLabel { background: transparent; }"
    );
    rightPanelOpacityEffect_ = new QGraphicsOpacityEffect(rightPanel_);
    rightPanelOpacityEffect_->setOpacity(1.0);
    rightPanel_->setGraphicsEffect(rightPanelOpacityEffect_);

    rightPanelWidthAnim_ = new QPropertyAnimation(rightPanel_, "maximumWidth", this);
    rightPanelWidthAnim_->setDuration(190);
    rightPanelWidthAnim_->setEasingCurve(QEasingCurve::OutCubic);
    rightPanelOpacityAnim_ = new QPropertyAnimation(rightPanelOpacityEffect_, "opacity", this);
    rightPanelOpacityAnim_->setDuration(150);
    rightPanelOpacityAnim_->setEasingCurve(QEasingCurve::OutCubic);

    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel_);
    rightLayout->setContentsMargins(14, 14, 14, 14);
    rightLayout->setSpacing(10);

    ElaText *logTitle = new ElaText("AI 活动", rightPanel_);
    logTitle->setTextStyle(ElaTextType::Body);
    logTitle->setTextPixelSize(15);
    logTitle->setStyleSheet("color: #1a1a1a;");
    logTitle->setFont(sideFont);
    rightLayout->addWidget(logTitle);

    activityPanel_ = new ActivityPanel(rightPanel_);
    rightLayout->addWidget(activityPanel_, 1);

    splitter_->addWidget(rightPanel_);

    // 初始比例：左 220 | 中 stretch | 右 250
    splitter_->setSizes({220, 600, 250});
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setStretchFactor(2, 0);

    outerLayout->addWidget(splitter_);
}

// ==================== 左右面板折叠 ====================
void ProjectPage::togglePanel(bool isLeft)
{
    QWidget *panel = isLeft ? leftPanel_ : rightPanel_;
    QGraphicsOpacityEffect *opacityEffect = isLeft ? leftPanelOpacityEffect_ : rightPanelOpacityEffect_;
    QPropertyAnimation *widthAnim = isLeft ? leftPanelWidthAnim_ : rightPanelWidthAnim_;
    QPropertyAnimation *opacityAnim = isLeft ? leftPanelOpacityAnim_ : rightPanelOpacityAnim_;
    ElaIconButton *btn = isLeft ? leftToggleBtn_ : rightToggleBtn_;
    const int expandedWidth = isLeft ? kLeftPanelWidth : kRightPanelWidth;
    const QString showTip = isLeft ? "显示项目文件" : "显示 AI 活动";
    const QString hideTip = isLeft ? "隐藏项目文件" : "隐藏 AI 活动";

    if (!panel) return;

    if (isLeft) leftPanelCollapsed_ = !leftPanelCollapsed_;
    else rightPanelCollapsed_ = !rightPanelCollapsed_;
    if (isLeft) {
        emit leftPanelCollapsedChanged(leftPanelCollapsed_);
    }
    const bool collapsing = isLeft ? leftPanelCollapsed_ : rightPanelCollapsed_;

    const int startWidth = panel->isVisible() ? panel->maximumWidth() : 0;
    const int endWidth = collapsing ? 0 : expandedWidth;

    if (widthAnim) widthAnim->stop();
    if (opacityAnim) opacityAnim->stop();

    if (!collapsing) {
        panel->setVisible(true);
        panel->setMaximumWidth(qMax(1, startWidth));
        if (opacityEffect) opacityEffect->setOpacity(0.0);
    }

    if (btn) {
        btn->setAwesome(collapsing ? ElaIconType::Sidebar : ElaIconType::SidebarFlip);
        btn->setToolTip(collapsing ? showTip : hideTip);
    }

    if (widthAnim) {
        widthAnim->disconnect();
        widthAnim->setStartValue(startWidth);
        widthAnim->setEndValue(endWidth);
        connect(widthAnim, &QPropertyAnimation::finished, this, [panel, collapsing, expandedWidth]() {
            if (collapsing) panel->setVisible(false);
            panel->setMinimumWidth(0);
            panel->setMaximumWidth(collapsing ? 0 : expandedWidth);
        });
        widthAnim->start();
    }

    if (opacityAnim) {
        opacityAnim->setStartValue(collapsing ? 1.0 : 0.0);
        opacityAnim->setEndValue(collapsing ? 0.0 : 1.0);
        opacityAnim->start();
    }

    savePanelCollapseState();
}

void ProjectPage::restorePanelCollapseState()
{
    QSettings settings("AzurStudio", "AzurAgent");
    leftPanelCollapsed_ = settings.value("projectLeftPanelCollapsed", false).toBool();
    rightPanelCollapsed_ = settings.value("projectRightPanelCollapsed", false).toBool();

    auto applyState = [](QWidget *panel, QGraphicsOpacityEffect *effect, bool collapsed, int expandedWidth) {
        if (!panel) return;
        panel->setMinimumWidth(0);
        if (collapsed) {
            panel->setVisible(false);
            panel->setMaximumWidth(0);
            if (effect) effect->setOpacity(0.0);
        } else {
            panel->setVisible(true);
            panel->setMaximumWidth(expandedWidth);
            if (effect) effect->setOpacity(1.0);
        }
    };
    applyState(leftPanel_, leftPanelOpacityEffect_, leftPanelCollapsed_, kLeftPanelWidth);
    applyState(rightPanel_, rightPanelOpacityEffect_, rightPanelCollapsed_, kRightPanelWidth);

    if (leftToggleBtn_) {
        leftToggleBtn_->setAwesome(leftPanelCollapsed_ ? ElaIconType::Sidebar : ElaIconType::SidebarFlip);
        leftToggleBtn_->setToolTip(leftPanelCollapsed_ ? "显示项目文件" : "隐藏项目文件");
    }
    if (rightToggleBtn_) {
        rightToggleBtn_->setAwesome(rightPanelCollapsed_ ? ElaIconType::Sidebar : ElaIconType::SidebarFlip);
        rightToggleBtn_->setToolTip(rightPanelCollapsed_ ? "显示 AI 活动" : "隐藏 AI 活动");
    }
}

void ProjectPage::savePanelCollapseState()
{
    QSettings settings("AzurStudio", "AzurAgent");
    settings.setValue("projectLeftPanelCollapsed", leftPanelCollapsed_);
    settings.setValue("projectRightPanelCollapsed", rightPanelCollapsed_);
}

// ==================== 消息气泡 ====================
void ProjectPage::appendMessage(const QString &text, bool isUser)
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
    }

    messageLayout_->insertWidget(messageLayout_->count() - 1, bubble);

    QScrollBar *vBar = messageScrollArea_->verticalScrollBar();
    QMetaObject::invokeMethod(this, [vBar]() {
        vBar->setValue(vBar->maximum());
    }, Qt::QueuedConnection);
}

void ProjectPage::clearChatDisplay()
{
    currentAiBubble_ = nullptr;
    while (messageLayout_->count() > 1) {
        QLayoutItem *item = messageLayout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}


// ==================== 发送消息 ====================
void ProjectPage::onSendClicked()
{
    if (isWaitingResponse_) return;

    // 从 QSettings 读取 API 配置
    QSettings settings("AzurStudio", "AzurAgent");
    const QString apiKey = settings.value("apiKey").toString().trimmed();
    const QString baseUrl = settings.value("baseUrl", "https://api.deepseek.com").toString().trimmed();
    const QString model = settings.value("model", "deepseek-v4-flash").toString().trimmed();

    if (apiKey.isEmpty()) {
        ElaMessageBar::warning(ElaMessageBarType::TopRight, "提示", "请先在设置中填写 API Key", 3000);
        return;
    }

    // engine_ 是 Chat 模式和 Project 模式共用的同一个实例：如果此刻并不是
    // Project 自己在等回复（isWaitingResponse_ 为 false），但引擎却处于占用状态，
    // 说明 Chat 模式正有一个请求在跑，直接 start() 会悄悄把它 cancel 掉且不通知对方。
    if (!isWaitingResponse_ && engine_->isBusy()) {
        ElaMessageBar::warning(ElaMessageBarType::TopRight, "提示",
                               "对话模式正有请求在处理中，请稍后再发送，或先取消", 3000);
        return;
    }

    const QString text = inputEdit_->toPlainText().trimmed();
    if (text.isEmpty()) return;

    // 同步"Agent 权限"设置（每次确认 / 自动执行）到引擎
    engine_->setAutoExecute(settings.value("agentPermission", 0).toInt() == 1);

    appendMessage(text, true);
    inputEdit_->clear();
    lastUserMessage_ = text;

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = text;
    messageHistory_.append(userMsg);
    emit conversationUpdated(messageHistory_);

    // 创建 AI 回复气泡占位
    currentAiBuffer_.clear();
    currentAiBubble_ = nullptr;
    appendMessage(QString(), false);

    engine_->start(apiKey, baseUrl, model, messageHistory_,
                   systemPrompt_, ToolExecutor::toolDefinitions(), projectPath_);

    // 之前这里遗漏了禁用输入框：isWaitingResponse_ 从未被置为 true，
    // 导致用户可以在 AI 还在生成时反复点发送/按回车，叠加发出多个请求。
    setInputEnabled(false);
}

// ==================== AI 响应处理 ====================
void ProjectPage::onChunkReceived(const QString &delta)
{
    if (!isActive_ || !currentAiBubble_) return;
    currentAiBuffer_ += delta;
    // 节流：50ms 内多个 token 只触发一次 UI 刷新
    if (!throttleTimer_->isActive()) {
        throttleTimer_->start();
    }
}

void ProjectPage::flushAiContent()
{
    if (!currentAiBubble_ || !isActive_) return;
    currentAiBubble_->setAiStreamingContent(currentAiBuffer_);
    QScrollBar *vBar = messageScrollArea_->verticalScrollBar();
    vBar->setValue(vBar->maximum());
}

void ProjectPage::onResponseCompleted(const QString &fullText)
{
    if (!isActive_ || !currentAiBubble_) return;

    // 先刷出缓冲区中的剩余内容
    if (throttleTimer_->isActive()) {
        throttleTimer_->stop();
        flushAiContent();
    }

    if (currentAiBubble_ && !fullText.isEmpty()) {
        currentAiBubble_->setAiContent(fullText);
    }

    // 从引擎同步完整消息历史（含工具调用记录和最终回复）
    messageHistory_ = engine_->messageHistory();
    emit conversationUpdated(messageHistory_);

    // 自动生成标题
    QString title;
    int userMsgCount = 0;
    for (const QJsonObject &msg : messageHistory_) {
        if (msg["role"].toString() == "user") ++userMsgCount;
    }
    if (userMsgCount <= 1 && !lastUserMessage_.isEmpty()) {
        title = lastUserMessage_.left(30);
        if (lastUserMessage_.length() > 30) title += "...";
    }
    if (!title.isEmpty()) emit titleChanged(title);

    setInputEnabled(true);
}

void ProjectPage::onError(const QString &errorMessage)
{
    if (!isActive_) return;
    if (throttleTimer_->isActive()) {
        throttleTimer_->stop();
    }
    ElaMessageBar::error(ElaMessageBarType::TopRight, "请求失败", errorMessage, 5000);
    if (currentAiBubble_) {
        currentAiBubble_->setAiStreamingContent("请求失败: " + errorMessage);
    }
    // 同步消息历史（如果工具调用过程中出错，已执行的工具结果仍在历史中）
    messageHistory_ = engine_->messageHistory();
    setInputEnabled(true);
}

void ProjectPage::setInputEnabled(bool enabled)
{
    isWaitingResponse_ = !enabled;
    inputEdit_->setEnabled(enabled);
    sendButton_->setVisible(enabled);
    sendButton_->setEnabled(enabled);
    stopButton_->setVisible(!enabled);
    if (enabled) inputEdit_->setFocus();
}

void ProjectPage::restoreConversation(const QList<QJsonObject> &messages)
{
    messageHistory_ = messages;
    clearChatDisplay();

    // 切换项目/切换对话时，之前遗留的活动步骤（读文件/写文件/执行命令等记录）
    // 也应该一并清空，否则不同项目/对话的活动记录会一直混在一起累积。
    if (activityPanel_) {
        activityPanel_->clear();
    }

    // 重绘所有历史消息
    for (const QJsonObject &msg : messages) {
        const QString role = msg["role"].toString();
        const QString content = msg["content"].toString();
        if (role == "user") {
            appendMessage(content, true);
        } else if (role == "assistant" && !content.isEmpty()) {
            // 跳过仅含工具调用的 assistant 消息
            appendMessage(content, false);
        }
    }
    setInputEnabled(true);
}
