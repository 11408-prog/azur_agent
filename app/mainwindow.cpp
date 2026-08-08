#include "app/mainwindow.h"
#include "chat/chatpagewidget.h"
#include "app/settingpagewidget.h"
#include "core/promptloader.h"
#include "data/appsettings.h"

#include <ElaWindow.h>
#include <ElaApplication.h>
#include <ElaNavigationBar.h>
#include <ElaScrollPage.h>
#include <ElaText.h>
#include <ElaLineEdit.h>
#include <ElaComboBox.h>
#include <ElaToggleSwitch.h>
#include <ElaPushButton.h>
#include <ElaTheme.h>
#include <ElaMessageBar.h>
#include <ElaListView.h>
#include <ElaIcon.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QScrollBar>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QDir>
#include <QMessageBox>
#include <QCloseEvent>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextBrowser>
#include <QDateTime>
#include <QFile>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QElapsedTimer>
#include <QClipboard>
#include <QShortcut>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QInputDialog>
#include <QDialog>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QDebug>
#include <QSet>
#include <QPainter>
#include <QtGlobal>
#include <QPainterPath>

#include "core/ai_client.h"
#include "data/conversationmanager.h"
#include "core/agent_engine.h"

// 从可执行文件位置回溯到项目根目录，构建资源路径
static QString projectRoot()
{
    QString appDir = QCoreApplication::applicationDirPath();
    return QDir::cleanPath(appDir + "/../..");
}

MainWindow::MainWindow(QWidget *parent)
    : ElaWindow(parent)
    , chatPage_(nullptr)
    , settingPage_(nullptr)
    , aboutPage_(nullptr)
    , client_(new DeepSeekClient(this))
    , isWaitingResponse_(false)
    , chatEngine_(nullptr)
    , chatPageWidget_(nullptr)
    , settingsPageWidget_(nullptr)
{
    qDebug()<<"[MAINWIN] 构造 MainWindow";
    setWindowTitle("Azur Agent");
    resize(1100, 750);

    // 修复 tooltip 样式：防止 ElaWidgetTools 主题导致黑块
    setStyleSheet(
        "QToolTip {"
        "   background-color: #2d2d2d;"
        "   color: #e0e0e0;"
        "   border: 1px solid #555;"
        "   border-radius: 6px;"
        "   padding: 6px 10px;"
        "   font-size: 13px;"
        "}"
    );

    // ==================== 初始化数据层 ====================

    // 聊天会话管理器（存聊天模式对话）
    conversationManager_ = new ConversationManager(this);
    if (!conversationManager_->initialize()) {
        QMessageBox::warning(nullptr, "错误", "无法初始化会话存储目录");
    }
    connect(conversationManager_, &ConversationManager::conversationListChanged,
            this, [this]() {
        if (chatPageWidget_) {
            chatPageWidget_->refreshConversationList(
                conversationManager_->conversationsMeta(), currentConversationId_);
        }
    });

    // ==================== 创建 UI 组件 ====================

    chatPageWidget_ = new ChatPageWidget(this);
    settingsPageWidget_ = new SettingPageWidget(this);

    // ---- 连接 ChatPageWidget 信号 ----
    connect(chatPageWidget_, &ChatPageWidget::newConversationClicked,
            this, &MainWindow::onNewConversation);
    connect(chatPageWidget_, &ChatPageWidget::conversationClicked,
            this, &MainWindow::loadConversation);
    connect(chatPageWidget_, &ChatPageWidget::sendClicked, this, [this](const QString &text) {
        if (isWaitingResponse_) return;
        if (text.trimmed().isEmpty()) return;
        lastUserMessage_ = text;
        onSendClicked();
    });
    connect(chatPageWidget_, &ChatPageWidget::cancelRequested, this, [this]() {
        if (chatEngine_) {
            chatEngine_->cancel();
            // 取消生成时，把卡在"思考中/正在连接..."占位动画上的气泡收尾成
            // 明确的"已取消生成"状态，不然界面上会永远停在思考动画里。
            chatPageWidget_->cancelAiResponse();
            chatPageWidget_->setInputEnabled(true);
            // 之前这里遗漏了重置 isWaitingResponse_，导致取消一次之后
            // onSendClicked 里的 "if (isWaitingResponse_) return;" 会一直挡住后续发送。
            isWaitingResponse_ = false;
        }
    });
    connect(chatPageWidget_, &ChatPageWidget::clearHistoryClicked, this, [this]() {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "确认清空", "确定要清空所有历史记录吗？此操作不可撤销。",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QJsonArray meta = conversationManager_->conversationsMeta();
            for (const QJsonValue &val : meta) {
                conversationManager_->deleteConversation(val.toObject()["id"].toString());
            }
            onNewConversation();
        }
    });
    connect(chatPageWidget_, &ChatPageWidget::conversationDeleteRequested, this, [this](const QString &id) {
        if (conversationManager_->deleteConversation(id)) {
            if (currentConversationId_ == id) {
                QJsonArray meta = conversationManager_->conversationsMeta();
                if (meta.isEmpty()) {
                    onNewConversation();
                } else {
                    loadConversation(meta.first().toObject()["id"].toString());
                }
            }
        }
    });
    connect(chatPageWidget_, &ChatPageWidget::conversationRenameRequested, this, [this](const QString &id, const QString &newTitle) {
        conversationManager_->renameConversation(id, newTitle);
        chatPageWidget_->refreshConversationList(
            conversationManager_->conversationsMeta(), currentConversationId_);
    });

    // ---- 连接 SettingPageWidget 信号 ----
    connect(settingsPageWidget_, &SettingPageWidget::bgOpacityChanged, this, [this](int val) {
        chatPageWidget_->applyChatBg(val);
    });
    connect(settingsPageWidget_, &SettingPageWidget::chatBgVisibilityChanged, this, [this](bool enabled) {
        if (enabled) {
            QString bgPath = projectRoot() + "/app/avatar/bg.png";
            QPixmap bgPixmap(bgPath);
            if (!bgPixmap.isNull()) {
                chatPageWidget_->setBackgroundPixmap(bgPixmap);
                chatPageWidget_->applyChatBg(AppSettings::bgOpacity());
            }
        } else {
            chatPageWidget_->clearChatBg();
        }
    });
    connect(settingsPageWidget_, &SettingPageWidget::connectionTestRequested, this, [this](const QString &apiKey, const QString &baseUrl) {
        client_->testConnection(apiKey, baseUrl);
    });

    // ==================== 创建 AgentEngine ====================

    chatEngine_ = new AgentEngine(client_, this);
    connect(chatEngine_, &AgentEngine::chunkReceived,
            this, &MainWindow::onApiChunkReceived);
    connect(chatEngine_, &AgentEngine::finished,
            this, &MainWindow::onApiResponseCompleted);
    connect(chatEngine_, &AgentEngine::errorOccurred,
            this, &MainWindow::onApiError);
    connect(chatEngine_, &AgentEngine::stepChanged,
            this, [this](const QString &text) { chatPageWidget_->updateAiStep(text); });

    // 注：不再注册任何写操作工具（write_file/apply_patch/run_command/git_*），
    // AgentEngine::writeConfirmationRequired 因此永远不会触发，无需连接确认弹窗。

    // ---- 连接测试结果 ----
    connect(client_, &DeepSeekClient::connectionTested, this,
            [this](bool success, const QString &message) {
        if (success) {
            ElaMessageBar::success(ElaMessageBarType::TopRight, "连接测试", message, 3000);
        } else {
            ElaMessageBar::error(ElaMessageBarType::TopRight, "连接测试", message, 5000);
        }
    });

    // ==================== 构建 UI 导航 ====================

    setupNavigation();
    systemPrompt_ = buildSystemPrompt();
    if (systemPrompt_.isEmpty()) {
        qWarning() << "systemPrompt_ 为空：未能从 prompt 文件读取到内容，"
                      "请确认 app.qrc 引用的资源文件存在。";
    }

    // ==================== 加载背景图（设置开启时才加载） ====================

    {
        QString bgPath = projectRoot() + "/app/avatar/bg.png";
        QPixmap bgPixmap(bgPath);
        if (!bgPixmap.isNull() && AppSettings::chatBgEnabled()) {
            chatPageWidget_->setBackgroundPixmap(bgPixmap);
            chatPageWidget_->applyChatBg(AppSettings::bgOpacity());
        }
    }

    // ==================== 加载设置 & 启动对话 ====================

    loadSettings();

    // 启动时默认打开一个新对话
    QJsonArray meta = conversationManager_->conversationsMeta();
    bool needNewConversation = true;
    if (!meta.isEmpty()) {
        const QString latestId = meta.first().toObject()["id"].toString();
        const QJsonArray latestMessages = conversationManager_->loadConversation(latestId);
        if (latestMessages.isEmpty()) {
            currentConversationId_ = latestId;
            needNewConversation = false;
        }
    }
    if (needNewConversation) {
        currentConversationId_ = conversationManager_->createNewConversation("新对话");
        if (currentConversationId_.isEmpty()) {
            currentConversationId_ = "dummy";
        }
    }
    loadConversation(currentConversationId_);

    chatPageWidget_->refreshConversationList(
        conversationManager_->conversationsMeta(), currentConversationId_);

    // ==================== 侧边栏折叠：点击导航栏 footer 节点触发 ====================

    connect(chatPageWidget_, &ChatPageWidget::sidebarCollapsedChanged,
            this, &MainWindow::updateToggleButtonState);

    connect(this, &ElaWindow::navigationNodeClicked, this,
            [this](ElaNavigationType::NavigationNodeType, QString nodeKey) {
        if (!sidebarToggleFooterKey_.isEmpty() && nodeKey == sidebarToggleFooterKey_) {
            chatPageWidget_->toggleSidebar();
        }
    });

    updateToggleButtonState();
}

// ==================== AppBar 按钮状态 ====================

void MainWindow::updateToggleButtonState()
{
    if (sidebarToggleFooterKey_.isEmpty()) return;

    bool collapsed = chatPageWidget_->isSidebarCollapsed();
    // ElaWindow 只公开了改导航节点文字的接口，没有公开改图标的接口，
    // 所以这里用文字变化代替原来按钮的图标切换来表达状态。
    setNavigationNodeTitle(sidebarToggleFooterKey_, collapsed ? "显示历史记录" : "隐藏历史记录");
}

// ==================== 导航设置 ====================

void MainWindow::setupNavigation()
{
    qDebug()<<"[MAINWIN] setupNavigation 开始";

    setUserInfoCardTitle("Azur Agent");
    setUserInfoCardSubTitle("Enterprise");
    const QPixmap avatarPixmap(projectRoot() + "/app/avatar/enterprise3.png");
    if (!avatarPixmap.isNull()) {
        setUserInfoCardPixmap(avatarPixmap);
    } else {
        qWarning() << "头像资源加载失败";
    }

    // 对话页面
    chatPage_ = new ElaScrollPage(this);
    chatPage_->setWindowTitle("对话");
    chatPage_->setTitleVisible(false);
    chatPage_->addCentralWidget(chatPageWidget_, true, true, 0);

    // 设置页面
    settingPage_ = new ElaScrollPage(this);
    settingPage_->setWindowTitle("设置");
    settingPage_->setTitleVisible(false);
    settingPage_->addCentralWidget(settingsPageWidget_, true, true, 0.5);

    // 关于页面
    aboutPage_ = new ElaScrollPage(this);
    aboutPage_->setWindowTitle("关于");
    aboutPage_->setTitleVisible(false);
    setupAboutPage();

    addPageNode("对话", chatPage_, ElaIconType::CommentDots);

    QString settingKey, aboutKey;
    // 侧边栏折叠是个"动作"而不是"页面"，用不带 page 参数的 addFooterNode()
    // 重载——点击只会发 navigationNodeClicked 信号，不会真的导航到某个页面，
    // 见构造函数里对这个信号的连接。
    addFooterNode("隐藏历史记录", sidebarToggleFooterKey_, 0, ElaIconType::SidebarFlip);
    addFooterNode("设置", settingPage_, settingKey, 0, ElaIconType::GearComplex);
    addFooterNode("关于", aboutPage_, aboutKey, 0, ElaIconType::Info);
}

// ==================== 关于页面 ====================

void MainWindow::setupAboutPage()
{
    QWidget *aboutContent = new QWidget();
    QVBoxLayout *aboutLayout = new QVBoxLayout(aboutContent);
    aboutLayout->setContentsMargins(30, 30, 30, 30);
    aboutLayout->setSpacing(20);
    aboutLayout->setAlignment(Qt::AlignCenter);

    ElaText *appName = new ElaText("Azur Agent", aboutContent);
    appName->setTextPixelSize(28);
    QFont boldFont = appName->font();
    boldFont.setBold(true);
    appName->setFont(boldFont);
    appName->setAlignment(Qt::AlignCenter);
    aboutLayout->addWidget(appName);

    ElaText *version = new ElaText("", aboutContent);
    version->setTextStyle(ElaTextType::Caption);
    version->setAlignment(Qt::AlignCenter);
    aboutLayout->addWidget(version);

    QFrame *line = new QFrame(aboutContent);
    line->setFrameShape(QFrame::HLine);
    line->setFixedWidth(300);
    aboutLayout->addWidget(line, 0, Qt::AlignCenter);

    ElaText *desc = new ElaText(
        "基于 Qt 6.11 + ElaWidgetTools 构建的角色扮演聊天助手\n"
        "与「企业」对话，感受碧蓝航线的语气与风格",
        aboutContent);
    desc->setTextStyle(ElaTextType::Body);
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);
    aboutLayout->addWidget(desc);

    ElaText *tech = new ElaText(
        "技术栈\n"
        "Qt " QT_VERSION_STR "\n"
        "ElaWidgetTools\n"
        "C++17",
        aboutContent);
    tech->setTextStyle(ElaTextType::Body);
    tech->setAlignment(Qt::AlignCenter);
    tech->setWordWrap(true);
    aboutLayout->addWidget(tech);

    ElaText *copyright = new ElaText("目前在开发阶段，可能会有一些问题", aboutContent);
    copyright->setTextStyle(ElaTextType::Caption);
    copyright->setAlignment(Qt::AlignCenter);
    aboutLayout->addWidget(copyright);

    aboutLayout->addStretch();
    aboutPage_->addCentralWidget(aboutContent, true, true, 0.5);
}

// ==================== 发送消息 ====================

void MainWindow::onSendClicked()
{
    qDebug()<<"[MAINWIN] onSendClicked | isWaitingResponse_="<<isWaitingResponse_;
    const QString text = lastUserMessage_;

    QString apiKey = settingsPageWidget_->apiKey();
    if (apiKey.isEmpty()) {
        ElaMessageBar::warning(ElaMessageBarType::TopRight, "提示",
                               "请先在设置中填写 API Key", 3000);
        return;
    }

    const QUrl baseUrl(settingsPageWidget_->baseUrl());
    if (!baseUrl.isValid() || baseUrl.host().isEmpty()
        || (baseUrl.scheme() != "http" && baseUrl.scheme() != "https")) {
        ElaMessageBar::warning(ElaMessageBarType::TopRight, "提示",
                               "请在设置中填写有效的 http(s) Base URL", 3000);
        return;
    }

    if (settingsPageWidget_->modelName().isEmpty()) {
        ElaMessageBar::warning(ElaMessageBarType::TopRight, "提示",
                               "请先在设置中填写模型名称", 3000);
        return;
    }

    chatPageWidget_->appendMessage(text, true);
    lastUserMessage_ = text;

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = text;
    messageHistory_.append(userMsg);

    // 创建 AI 回复气泡占位
    chatPageWidget_->appendMessage(QString(), false, true);

    // 启动 AgentEngine
    chatEngine_->start(apiKey, settingsPageWidget_->baseUrl(), settingsPageWidget_->modelName(),
                       messageHistory_, systemPrompt_, QJsonArray(), QString());
    chatPageWidget_->setInputEnabled(false);
    isWaitingResponse_ = true;
}

// ==================== AI 回调 ====================

void MainWindow::onApiChunkReceived(const QString &delta)
{
    chatPageWidget_->onChunkReceived(delta);
}

void MainWindow::onApiResponseCompleted(const QString &fullText)
{
    qDebug()<<"[MAINWIN] onApiResponseCompleted | 文本长度="<<fullText.length();

    chatPageWidget_->onResponseCompleted(fullText);

    settingsPageWidget_->rememberModel(settingsPageWidget_->modelName());
    messageHistory_ = chatEngine_->messageHistory();

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

    QJsonArray messagesArray;
    for (const QJsonObject &msg : qAsConst(messageHistory_)) {
        messagesArray.append(msg);
    }
    conversationManager_->saveConversation(currentConversationId_, messagesArray, title);

    chatPageWidget_->refreshConversationList(
        conversationManager_->conversationsMeta(), currentConversationId_);
    chatPageWidget_->setInputEnabled(true);
    isWaitingResponse_ = false;
}

void MainWindow::onApiError(const QString &errorMessage)
{
    qDebug()<<"[MAINWIN] onApiError | errorMessage="<<errorMessage;

    chatPageWidget_->onResponseError(errorMessage);
    ElaMessageBar::error(ElaMessageBarType::TopRight, "请求失败", errorMessage, 5000);
    chatPageWidget_->setInputEnabled(true);
    isWaitingResponse_ = false;
}

// ==================== 会话管理 ====================

void MainWindow::loadConversation(const QString &id)
{
    qDebug() << "[MAINWIN] loadConversation | 切换前历史条数=" << messageHistory_.size()
        << "| 目标id=" << id;
    if (id.isEmpty()) return;
    qDebug()<<"[MAINWIN] loadConversation | id="<<id;

    chatEngine_->cancel();
    chatPageWidget_->clearAiState();
    isWaitingResponse_ = false;

    currentConversationId_ = id;
    QJsonArray messages = conversationManager_->loadConversation(id);
    messageHistory_.clear();
    for (const QJsonValue &val : messages) {
        messageHistory_.append(val.toObject());
    }

    if (messages.isEmpty()) {
        messageHistory_.clear();
        chatPageWidget_->showGreeting();
    } else {
        chatPageWidget_->restoreConversation(messages);
    }

    chatPageWidget_->refreshConversationList(
        conversationManager_->conversationsMeta(), currentConversationId_);

    messageHistory_.clear();
    for (const QJsonValue &val : messages) {
        messageHistory_.append(val.toObject());
    }
    qDebug() << "[MAINWIN] loadConversation | 切换后历史条数=" << messageHistory_.size();
}

void MainWindow::onNewConversation()
{
    qDebug()<<"[MAINWIN] onNewConversation";

    chatEngine_->cancel();
    chatPageWidget_->clearAiState();
    isWaitingResponse_ = false;

    systemPrompt_ = buildSystemPrompt();
    QString newId = conversationManager_->createNewConversation("新对话");
    if (newId.isEmpty()) return;
    currentConversationId_ = newId;
    messageHistory_.clear();
    chatPageWidget_->showGreeting();
    chatPageWidget_->refreshConversationList(
        conversationManager_->conversationsMeta(), currentConversationId_);
}

// ==================== 窗口关闭 ====================

void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug()<<"[MAINWIN] closeEvent 窗口关闭";
    saveSettings();
    event->accept();
}

MainWindow::~MainWindow()
{
    qDebug()<<"[MAINWIN] 析构 MainWindow";
}

// ==================== 设置持久化 ====================

void MainWindow::saveSettings()
{
    qDebug()<<"[MAINWIN] saveSettings";
    settingsPageWidget_->saveSettings();
}

void MainWindow::loadSettings()
{
    qDebug()<<"[MAINWIN] loadSettings";
    settingsPageWidget_->loadSettings();
    chatPageWidget_->restoreSidebarState(true);
}

// ==================== Prompt 构建 ====================

QString MainWindow::buildSystemPrompt() const
{
    // 统一走 PromptLoader，避免两处实现不一致
    return PromptLoader::buildChatSystemPrompt(AppSettings::chatPromptMode());
}
