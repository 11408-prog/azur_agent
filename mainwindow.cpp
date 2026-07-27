#include "mainwindow.h"
#include "chatpagewidget.h"
#include "settingpagewidget.h"
#include "promptloader.h"
#include "projecthistorydialog.h"
#include "projectconvdialog.h"

#include <ElaWindow.h>
#include <ElaApplication.h>
#include <ElaNavigationBar.h>
#include <ElaScrollPage.h>
#include <ElaText.h>
#include <ElaIconButton.h>
#include <ElaLineEdit.h>
#include <ElaComboBox.h>
#include <ElaToggleSwitch.h>
#include <ElaPushButton.h>
#include <ElaTheme.h>
#include <ElaMessageBar.h>
#include <ElaListView.h>
#include <ElaIcon.h>
#include <ElaContentDialog.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QScrollBar>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QCloseEvent>
#include <QSettings>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextBrowser>
#include <QDateTime>
#include <QStandardPaths>
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
#include <ElaContentDialog.h>
#include <QInputDialog>
#include <QDialog>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QDebug>
#include <QSet>
#include <QPainter>
#include <QPainterPath>

#include "ai_client.h"
#include "conversationmanager.h"
#include "agent_engine.h"
#include "tool_executor.h"
#include "projectsession.h"
#include "projectpage.h"

MainWindow::MainWindow(QWidget *parent)
    : ElaWindow(parent)
    , chatPage_(nullptr)
    , chatHistoryPage_(nullptr)
    , settingPage_(nullptr)
    , aboutPage_(nullptr)
    , client_(new DeepSeekClient(this))
    , isWaitingResponse_(false)
    , projectPage_(nullptr)
    , projectPageWidget_(nullptr)
    , currentProject_(nullptr)
    , chatEngine_(nullptr)
    , chatPageWidget_(nullptr)
    , settingsPageWidget_(nullptr)
    , sidebarToggleBtn_(nullptr)
    , openFolderBtn_(nullptr)
    , projectHistoryBtn_(nullptr)
{
    qDebug()<<"[MAINWIN] 构造 MainWindow";
    setWindowTitle("Azur Agent");
    resize(1100, 750);

    // 初始化聊天会话管理器（存聊天模式对话）
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

    // 初始化共享项目会话管理器（存项目模式对话，所有项目共用）
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString projectDataDir = appData + "/AzurAgent/data/project_chats";
    projectConvMgr_ = new ConversationManager(this);
    if (!projectConvMgr_->initialize(projectDataDir)) {
        qWarning() << "Failed to initialize project conversation manager";
    }
    connect(projectConvMgr_, &ConversationManager::conversationListChanged,
            this, [this]() {
        // 项目对话列表已变更，无需额外操作
    });

    // 一次性迁移旧项目对话
    migrateOldProjectConversations();

    // ---- 创建解耦组件 ----
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
    connect(settingsPageWidget_, &SettingPageWidget::connectionTestRequested, this, [this](const QString &apiKey, const QString &baseUrl) {
        client_->testConnection(apiKey, baseUrl);
    });

    // ---- 连接 DeepSeek 客户端（通过 AgentEngine） ----
    chatEngine_ = new AgentEngine(client_, this);
    connect(chatEngine_, &AgentEngine::chunkReceived,
            this, &MainWindow::onApiChunkReceived);
    connect(chatEngine_, &AgentEngine::finished,
            this, &MainWindow::onApiResponseCompleted);
    connect(chatEngine_, &AgentEngine::errorOccurred,
            this, &MainWindow::onApiError);
    connect(chatEngine_, &AgentEngine::stepChanged,
            this, [this](const QString &text) { chatPageWidget_->updateAiStep(text); });
    connect(chatEngine_, &AgentEngine::writeConfirmationRequired, this, [this](const QStringList &diffList) {
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

        // Enter 键接受修改，Escape 键拒绝
        QShortcut *enterShortcut = new QShortcut(QKeySequence(Qt::Key_Return), &dlg);
        QShortcut *enterShortcut2 = new QShortcut(QKeySequence(Qt::Key_Enter), &dlg);
        connect(enterShortcut, &QShortcut::activated, &dlg, &QDialog::accept);
        connect(enterShortcut2, &QShortcut::activated, &dlg, &QDialog::accept);

        const bool accepted = (dlg.exec() == QDialog::Accepted);
        chatEngine_->confirmWrite(accepted);
    });

    // 连接测试结果
    connect(client_, &DeepSeekClient::connectionTested, this,
            [this](bool success, const QString &message) {
        if (success) {
            ElaMessageBar::success(ElaMessageBarType::TopRight, "连接测试", message, 3000);
        } else {
            ElaMessageBar::error(ElaMessageBarType::TopRight, "连接测试", message, 5000);
        }
    });

    // 加载聊天背景图
    {
        static const QString kBgPath = QStringLiteral("C:/Users/ASUS/Desktop/practice/agent_/avatar/bg.png");
        QPixmap bgPixmap(kBgPath);
        if (!bgPixmap.isNull()) {
            chatPageWidget_->setBackgroundPixmap(bgPixmap);
            QSettings s("AzurStudio", "AzurAgent");
            chatPageWidget_->applyChatBg(s.value("bgOpacity", 25).toInt());
        }
    }

    // 构建 UI 导航
    setupNavigation();
    systemPrompt_ = buildSystemPrompt();
    if (systemPrompt_.isEmpty()) {
        qWarning() << "systemPrompt_ 为空：未能从 core_agent.md 等 prompt 文件读取到内容，"
                      "请确认 app.qrc 引用的资源文件存在。";
    }

    // 加载设置
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
    connect(chatPageWidget_, &ChatPageWidget::sidebarCollapsedChanged,
            this, &MainWindow::updateToggleButtonState);
    connect(projectPageWidget_, &ProjectPage::leftPanelCollapsedChanged,
            this, &MainWindow::updateToggleButtonState);

    connect(sidebarToggleBtn_, &ElaIconButton::clicked, this, [this]() {
        if (currentMode_ == AgentMode::Project && projectPageWidget_) {
            projectPageWidget_->togglePanel(true);   // 控制项目文件树
        } else {
            chatPageWidget_->toggleSidebar();        // 控制聊天历史侧边栏
        }
    });

    // 项目对话更新时自动持久化
    connect(projectPageWidget_, &ProjectPage::conversationUpdated,
            this, [this](const QList<QJsonObject> &messages) {
        if (currentMode_ != AgentMode::Project) return;
        QJsonArray arr;
        for (const auto &msg : messages) arr.append(msg);
        QString projectPath = currentProject_ ? currentProject_->projectPath : QString();
        projectConvMgr_->saveConversation(currentConversationId_, arr, QString(), projectPath);
    });

    updateToggleButtonState();
}

void MainWindow::updateToggleButtonState()
{
    if (!sidebarToggleBtn_) return;

    if (currentMode_ == AgentMode::Project && projectPageWidget_) {
        bool collapsed = projectPageWidget_->isLeftPanelCollapsed();
        sidebarToggleBtn_->setAwesome(collapsed ? ElaIconType::Sidebar : ElaIconType::SidebarFlip);
        sidebarToggleBtn_->setToolTip(collapsed ? "显示项目文件" : "隐藏项目文件");
    } else {
        bool collapsed = chatPageWidget_->isSidebarCollapsed();
        sidebarToggleBtn_->setAwesome(collapsed ? ElaIconType::Sidebar : ElaIconType::SidebarFlip);
        sidebarToggleBtn_->setToolTip(collapsed ? "显示历史记录" : "隐藏历史记录");
    }
}

// ==================== 导航设置 ====================
void MainWindow::setupNavigation()
{
    qDebug()<<"[MAINWIN] setupNavigation 开始";

    setUserInfoCardTitle("Azur Agent");
    setUserInfoCardSubTitle("Enterprise");
    const QPixmap avatarPixmap("C:/Users/ASUS/Desktop/practice/agent_/avatar/enterprise3.png");
    if (!avatarPixmap.isNull()) {
        setUserInfoCardPixmap(avatarPixmap);
    } else {
        qWarning() << "头像资源加载失败";
    }

    // 对话页面
    chatPage_ = new ElaScrollPage(this);
    chatPage_->setWindowTitle("对话");
    chatPage_->setTitleVisible(false);
    chatPage_->addCentralWidget(chatPageWidget_, true, true, 0.5);

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

    // AppBar 侧边栏折叠按钮
    QWidget *appBarActions = new QWidget(this);
    appBarActions->setFixedHeight(32);
    appBarActions->setStyleSheet("background: transparent;");
    QHBoxLayout *appBarActionsLayout = new QHBoxLayout(appBarActions);
    appBarActionsLayout->setContentsMargins(0, 0, 0, 2);
    appBarActionsLayout->setSpacing(4);

    sidebarToggleBtn_ = new ElaIconButton(ElaIconType::SidebarFlip, 16, 30, 30, appBarActions);
    sidebarToggleBtn_->setToolTip("隐藏历史记录");
    // 注意：sidebarToggleBtn_->clicked 的连接在构造函数中已完成
    //（包含 Project 和 Chat 模式的分支处理），此处不再重复连接
    connect(chatPageWidget_, &ChatPageWidget::sidebarCollapsedChanged, this, [this](bool collapsed) {
        sidebarToggleBtn_->setAwesome(collapsed ? ElaIconType::Sidebar : ElaIconType::SidebarFlip);
        sidebarToggleBtn_->setToolTip(collapsed ? "显示历史记录" : "隐藏历史记录");
    });
    openFolderBtn_ = new ElaIconButton(ElaIconType::FolderOpen, 16, 30, 30, appBarActions);
    openFolderBtn_->setToolTip("打开项目文件夹");
    openFolderBtn_->setVisible(false);
    connect(openFolderBtn_, &ElaIconButton::clicked, this, [this]() {
        // 保存当前项目关联的对话
        saveProjectConversation();
        saveCurrentProjectEntry();

        const QString dir = QFileDialog::getExistingDirectory(this, "选择项目目录");
        if (!dir.isEmpty()) {
            QSettings s("AzurStudio", "AzurAgent");
            s.setValue("lastProjectPath", dir);

            // 共享项目管理器已有初始化好的 projectConvMgr_

            // 在项目管理器中创建新对话
            currentConversationId_ = projectConvMgr_->createNewConversation("项目对话");

            currentProject_ = new ProjectSession(ProjectSession::load(dir));
            if (!currentProject_->isValid()) {
                delete currentProject_;
                currentProject_ = new ProjectSession();
                currentProject_->projectPath = dir;
                currentProject_->save();
            }
            projectPageWidget_->setProjectPath(currentProject_->projectPath);
            ToolExecutor::setAllowedPaths(currentProject_->allowedPaths);
            chatEngine_->setAllowedPaths(currentProject_->allowedPaths);

            // 清空项目对话并保存关联
            projectPageWidget_->restoreConversation({});
            saveCurrentProjectEntry();
        }
    });

    projectHistoryBtn_ = new ElaIconButton(ElaIconType::ClockRotateLeft, 16, 30, 30, appBarActions);
    projectHistoryBtn_->setToolTip("项目历史记录");
    projectHistoryBtn_->setVisible(false);
    connect(projectHistoryBtn_, &ElaIconButton::clicked, this, [this]() {
        ProjectHistoryDialog dlg(this);
        connect(&dlg, &ProjectHistoryDialog::projectSelected,
                this, [this](const QString &path, const QString &convId) {
            switchToProjectEntry(path, convId);
        });
        dlg.exec();
    });

    projectConvListBtn_ = new ElaIconButton(ElaIconType::CommentDots, 16, 30, 30, appBarActions);
    projectConvListBtn_->setToolTip("当前项目对话列表");
    projectConvListBtn_->setVisible(false);
    connect(projectConvListBtn_, &ElaIconButton::clicked, this, [this]() {
        if (!currentProject_ || currentProject_->projectPath.isEmpty()) return;
        ProjectConvDialog dlg(projectConvMgr_,
                               currentProject_->projectPath,
                               currentConversationId_, this);
        connect(&dlg, &ProjectConvDialog::conversationSelected,
                this, &MainWindow::switchToProjectConversation);
        dlg.exec();
    });

    appBarActionsLayout->addWidget(openFolderBtn_, 0, Qt::AlignTop);
    appBarActionsLayout->addWidget(projectConvListBtn_, 0, Qt::AlignTop);
    appBarActionsLayout->addWidget(projectHistoryBtn_, 0, Qt::AlignTop);
    appBarActionsLayout->addWidget(sidebarToggleBtn_, 0, Qt::AlignTop);
    appBarActionsLayout->addStretch();
    setCustomWidget(ElaAppBarType::LeftArea, appBarActions);

    addPageNode("对话", chatPage_, ElaIconType::CommentDots);

    // 项目模式页面
    projectPage_ = new ElaScrollPage(this);
    projectPage_->setWindowTitle("项目模式");
    projectPageWidget_ = new ProjectPage(chatEngine_, projectPage_);
    projectPage_->addCentralWidget(projectPageWidget_, true, true, 0.5);
    projectPage_->setTitleVisible(false);
    projectPageWidget_->installEventFilter(this);
    addPageNode("项目", projectPage_, ElaIconType::Code);

    QString settingKey, aboutKey;
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
    //标题
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
        "基于 Qt 6.11 + ElaWidgetTools 构建的智能助手\n"
        "支持 AI 对话、文件操作、代码生成等功能",
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

// ==================== DeepSeek API 回调 ====================
void MainWindow::onApiChunkReceived(const QString &delta)
{
    if (currentMode_ != AgentMode::Chat) return;
    chatPageWidget_->onChunkReceived(delta);
}

void MainWindow::onApiResponseCompleted(const QString &fullText)
{
    // 仅处理聊天模式下的完成回调，项目模式由 ProjectPage 自行处理
    if (currentMode_ != AgentMode::Chat) return;

    qDebug()<<"[MAINWIN] onApiResponseCompleted | 文本长度="<<fullText.length();

    // UI 更新委托给 ChatPageWidget
    chatPageWidget_->onResponseCompleted(fullText);

    // 业务逻辑：记住模型、同步历史、持久化
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

    // 保存到文件
    QJsonArray messagesArray;
    for (const QJsonObject &msg : messageHistory_) {
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
    if (currentMode_ != AgentMode::Chat) return;
    qDebug()<<"[MAINWIN] onApiError | errorMessage="<<errorMessage;

    chatPageWidget_->onResponseError(errorMessage);
    ElaMessageBar::error(ElaMessageBarType::TopRight, "请求失败", errorMessage, 5000);

    chatPageWidget_->setInputEnabled(true);
    isWaitingResponse_ = false;
}

// ==================== 会话管理 ====================
void MainWindow::loadConversation(const QString &id)
{
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

// ==================== 双模式切换 ====================
void MainWindow::enterProjectMode()
{
    qDebug()<<"[MAINWIN] enterProjectMode | 当前模式="<<(currentMode_==AgentMode::Chat?"Chat":"Project");
    if (currentMode_ == AgentMode::Project) return;
    currentMode_ = AgentMode::Project;

    chatEngine_->cancel();
    chatPageWidget_->clearAiState();
    isWaitingResponse_ = false;

    if (!currentProject_ || currentProject_->projectPath.isEmpty()) {
        QSettings s("AzurStudio", "AzurAgent");
        QString lastProject = s.value("lastProjectPath").toString();

        const QString dirPath = QFileDialog::getExistingDirectory(this,
            "选择项目目录", lastProject.isEmpty() ? QDir::homePath() : lastProject);
        if (dirPath.isEmpty()) {
            currentMode_ = AgentMode::Chat;
            return;
        }

        currentProject_ = new ProjectSession(ProjectSession::load(dirPath));
        if (!currentProject_->isValid()) {
            delete currentProject_;
            currentProject_ = new ProjectSession();
            currentProject_->projectPath = dirPath;
            currentProject_->save();
        }
        s.setValue("lastProjectPath", dirPath);
    }

    // 延迟执行重任务（索引重建等），让 UI 先完成导航动画
    QTimer::singleShot(0, this, &MainWindow::finishProjectInit);
}

void MainWindow::finishProjectInit()
{
    if (!currentProject_) return;

    // 尝试从历史记录恢复已有的项目对话
    QSettings s2("AzurStudio", "AzurAgent");
    const QJsonArray projHistory = s2.value("projectHistory").toJsonArray();
    QString prevConvId;
    for (const QJsonValue &val : projHistory) {
        const QJsonObject entry = val.toObject();
        if (entry["path"].toString() == QDir::toNativeSeparators(QDir::cleanPath(currentProject_->projectPath))) {
            prevConvId = entry["conversationId"].toString();
            break;
        }
    }

    const QString projPath = QDir::toNativeSeparators(QDir::cleanPath(currentProject_->projectPath));
    bool convExists = false;
    if (!prevConvId.isEmpty()) {
        const QJsonArray projMeta = projectConvMgr_->conversationsForProject(projPath);
        for (const QJsonValue &v : projMeta) {
            if (v.toObject()["id"].toString() == prevConvId) {
                convExists = true;
                break;
            }
        }
    }

    if (convExists) {
        currentConversationId_ = prevConvId;
        QJsonArray prevMessages = projectConvMgr_->loadConversation(currentConversationId_);
        QList<QJsonObject> msgList;
        for (const QJsonValue &v : prevMessages) msgList.append(v.toObject());
        projectPageWidget_->restoreConversation(msgList);
    } else if (!prevConvId.isEmpty()) {
        // 尝试从全局管理器迁移旧对话
        QJsonArray oldMessages = conversationManager_->loadConversation(prevConvId);
        if (!oldMessages.isEmpty()) {
            projectConvMgr_->saveConversation(prevConvId, oldMessages, QString(), projPath);
            // 迁移后从全局管理器删除旧对话
            conversationManager_->deleteConversation(prevConvId);
            currentConversationId_ = prevConvId;
            QList<QJsonObject> msgList;
            for (const QJsonValue &v : oldMessages) msgList.append(v.toObject());
            projectPageWidget_->restoreConversation(msgList);
            qDebug() << "[MAINWIN] 已将对话从全局管理器迁移到项目管理器:" << prevConvId;
        } else {
            currentConversationId_ = projectConvMgr_->createNewConversation("项目对话");
        }
    } else {
        currentConversationId_ = projectConvMgr_->createNewConversation("项目对话");
    }

    projectPageWidget_->loadSystemPrompt();
    ToolExecutor::setAllowedPaths(currentProject_->allowedPaths);
    chatEngine_->setAllowedPaths(currentProject_->allowedPaths);
    projectPageWidget_->setActive(true);
    if (projectPageWidget_) {
        projectPageWidget_->hideLeftToggleButton();
    }
    if (openFolderBtn_) {
        openFolderBtn_->setVisible(true);
    }
    if (projectHistoryBtn_) {
        projectHistoryBtn_->setVisible(true);
    }
    if (projectConvListBtn_) {
        projectConvListBtn_->setVisible(true);
    }
    // 保存当前 chat 会话关联到此项目
    saveCurrentProjectEntry();
    updateToggleButtonState();

    // 延迟执行项目索引重建，让 UI 先完成所有更新
    QTimer::singleShot(0, this, [this]() {
        if (currentProject_ && !currentProject_->projectPath.isEmpty()) {
            projectPageWidget_->setProjectPath(currentProject_->projectPath);
        }
    });
}

void MainWindow::enterChatMode()
{
    qDebug()<<"[MAINWIN] enterChatMode | 当前模式="<<(currentMode_==AgentMode::Chat?"Chat":"Project");
    if (currentMode_ == AgentMode::Chat) return;
    currentMode_ = AgentMode::Chat;

    chatEngine_->cancel();
    projectPageWidget_->setActive(false);
    ToolExecutor::setAllowedPaths({});
    chatEngine_->setAllowedPaths({});

    if (sidebarToggleBtn_) {
        sidebarToggleBtn_->setVisible(true);
    }
    if (openFolderBtn_) {
        openFolderBtn_->setVisible(false);
    }
    if (projectHistoryBtn_) {
        projectHistoryBtn_->setVisible(false);
    }
    if (projectConvListBtn_) {
        projectConvListBtn_->setVisible(false);
    }
    updateToggleButtonState();

    // 刷新聊天侧栏，反映迁移后已清理的对话列表
    chatPageWidget_->refreshConversationList(
        conversationManager_->conversationsMeta(), currentConversationId_);

    // 如果当前会话 ID 属于项目对话（不在全局管理器中），则切换到最近的聊天对话
    bool isChatConv = false;
    const QJsonArray chatMeta = conversationManager_->conversationsMeta();
    for (const QJsonValue &v : chatMeta) {
        if (v.toObject()["id"].toString() == currentConversationId_) {
            isChatConv = true;
            break;
        }
    }
    if (!isChatConv) {
        if (!chatMeta.isEmpty()) {
            loadConversation(chatMeta.first().toObject()["id"].toString());
        } else {
            onNewConversation();
        }
    }
}

// ==================== 设置持久化 ====================
void MainWindow::saveSettings()
{
    qDebug()<<"[MAINWIN] saveSettings";
    settingsPageWidget_->saveSettings();
    QSettings settings("AzurStudio", "AzurAgent");
    settings.setValue("sidebarCollapsed", false); // ChatPageWidget 内部管理侧边栏状态
}

void MainWindow::loadSettings()
{
    qDebug()<<"[MAINWIN] loadSettings";
    settingsPageWidget_->loadSettings();
    // 侧边栏状态由 ChatPageWidget 内部管理
    chatPageWidget_->restoreSidebarState(false);
}

// ==================== Prompt 构建 ====================
QString MainWindow::loadPromptFile(const QString &filename) const
{
    return PromptLoader::loadFile(filename);
}

QString MainWindow::buildSystemPrompt() const
{
    return PromptLoader::buildSystemPrompt();
}

#if 0
// 旧版 loadPromptFile / buildSystemPrompt（已迁移到 PromptLoader）
QString MainWindow::loadPromptFile_OLD(const QString &filename) const { return {}; }
QString MainWindow::buildSystemPrompt_OLD() const { return {}; }
#endif

// ==================== 事件过滤 ====================
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 检测项目页面的显示/隐藏，自动切换模式
    if (watched == projectPageWidget_) {
        if (event->type() == QEvent::Show) {
            // 延迟执行，让 ElaWindow 的导航动画完成后才弹对话框和做重任务
            QTimer::singleShot(0, this, &MainWindow::enterProjectMode);
        } else if (event->type() == QEvent::Hide) {
            projectPageWidget_->setActive(false);
            enterChatMode();
        }
    }

    return ElaWindow::eventFilter(watched, event);
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



// ==================== 已废弃（保留空实现） ====================
void MainWindow::onSelectWorkspaceClicked()
{
    // 此功能已通过 ProjectPage 管理
}

void MainWindow::restoreSidebarState(bool collapsed)
{
    chatPageWidget_->restoreSidebarState(collapsed);
}

// ==================== 项目历史记录 ====================
#if 0
// 已迁移到 ProjectHistoryDialog
void MainWindow::rebuildProjectHistoryMenu() {}
#endif

void MainWindow::saveCurrentProjectEntry()
{
    if (!currentProject_ || currentProject_->projectPath.isEmpty()) return;

    QSettings s("AzurStudio", "AzurAgent");
    QJsonArray history = s.value("projectHistory").toJsonArray();

    // 获取当前对话标题（从项目管理器查找）
    QString convTitle;
    const QJsonArray meta = projectConvMgr_->conversationsMeta();
    for (const QJsonValue &v : meta) {
        QJsonObject m = v.toObject();
        if (m["id"].toString() == currentConversationId_) {
            convTitle = m["title"].toString();
            break;
        }
    }

    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(currentProject_->projectPath));
    QJsonObject newEntry;
    newEntry["path"] = cleanPath;
    newEntry["name"] = QDir(cleanPath).dirName();
    newEntry["conversationId"] = currentConversationId_;
    newEntry["conversationTitle"] = convTitle;
    newEntry["lastOpened"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // 去重：移除已存在的相同路径
    for (int i = 0; i < history.size(); ++i) {
        if (history[i].toObject()["path"].toString() == cleanPath) {
            history.removeAt(i);
            break;
        }
    }

    // 插到最前面
    history.prepend(newEntry);

    // 最多保留 10 条
    while (history.size() > 10) {
        history.removeLast();
    }

    s.setValue("projectHistory", history);
}

void MainWindow::saveProjectConversation()
{
    if (currentMode_ != AgentMode::Project || !projectPageWidget_) return;
    const QList<QJsonObject> messages = projectPageWidget_->conversation();
    if (messages.isEmpty()) return;
    QJsonArray arr;
    for (const auto &msg : messages) arr.append(msg);
    QString projectPath = currentProject_ ? currentProject_->projectPath : QString();
    projectConvMgr_->saveConversation(currentConversationId_, arr, QString(), projectPath);
}

void MainWindow::switchToProjectEntry(const QString &path, const QString &convId)
{
    if (!projectPageWidget_) return;

    // 保存当前项目对话
    saveProjectConversation();
    saveCurrentProjectEntry();

    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(path));

    // 切换到目标项目
    currentProject_ = new ProjectSession(ProjectSession::load(cleanPath));
    if (!currentProject_->isValid()) {
        delete currentProject_;
        currentProject_ = new ProjectSession();
        currentProject_->projectPath = cleanPath;
        currentProject_->save();
    }

    // 初始化项目专用的会话管理器
    // 共享项目管理器已在构造函数初始化，无需重复初始化

    // 从历史记录中取出关联的 conversationId
    QSettings s("AzurStudio", "AzurAgent");
    const QJsonArray history = s.value("projectHistory").toJsonArray();
    QString targetConvId = convId;
    for (const QJsonValue &val : history) {
        const QJsonObject entry = val.toObject();
        if (entry["path"].toString() == cleanPath) {
            targetConvId = entry["conversationId"].toString();
            break;
        }
    }

    // 检查目标对话是否存在于项目管理器中（按项目过滤）
    const QJsonArray projectMeta = projectConvMgr_->conversationsForProject(cleanPath);
    bool convExists = false;
    if (!targetConvId.isEmpty()) {
        for (const QJsonValue &val : projectMeta) {
            if (val.toObject()["id"].toString() == targetConvId) {
                convExists = true;
                break;
            }
        }
    }

    // 设置会话 ID
    if (convExists) {
        currentConversationId_ = targetConvId;
    } else if (!targetConvId.isEmpty()) {
        // 尝试从全局管理器迁移旧对话
        QJsonArray oldMessages = conversationManager_->loadConversation(targetConvId);
        if (!oldMessages.isEmpty()) {
            projectConvMgr_->saveConversation(targetConvId, oldMessages, QString(), cleanPath);
            // 迁移后从全局管理器删除旧对话
            conversationManager_->deleteConversation(targetConvId);
            currentConversationId_ = targetConvId;

            qDebug() << "[MAINWIN] 已将对话从全局管理器迁移到项目管理器:" << targetConvId;
        } else {
            currentConversationId_ = projectConvMgr_->createNewConversation("项目对话");
        }
    } else {
        currentConversationId_ = projectConvMgr_->createNewConversation("项目对话");
    }

    // 更新 UI
    projectPageWidget_->setProjectPath(currentProject_->projectPath);
    ToolExecutor::setAllowedPaths(currentProject_->allowedPaths);
    chatEngine_->setAllowedPaths(currentProject_->allowedPaths);

    // 从项目管理器加载历史对话
    QJsonArray messages = projectConvMgr_->loadConversation(currentConversationId_);
    QList<QJsonObject> msgList;
    for (const QJsonValue &v : messages) {
        msgList.append(v.toObject());
    }
    projectPageWidget_->restoreConversation(msgList);

    // 保存新的关联
    saveCurrentProjectEntry();
}

#if 0
#endif

void MainWindow::switchToProjectConversation(const QString &convId)
{
    if (!projectPageWidget_) return;

    // 先保存当前对话
    saveProjectConversation();

    // 切换到目标对话
    currentConversationId_ = convId;
    QJsonArray messages = projectConvMgr_->loadConversation(convId);
    QList<QJsonObject> msgList;
    for (const QJsonValue &v : messages) {
        msgList.append(v.toObject());
    }
    projectPageWidget_->restoreConversation(msgList);
    saveCurrentProjectEntry();

    qDebug() << "[MAINWIN] 切换到项目对话:" << convId;
}

// ==================== 一次性迁移旧项目对话 ====================
void MainWindow::migrateOldProjectConversations()
{
    QSettings s("AzurStudio", "AzurAgent");
    if (s.value("projectConvMigrationDone", false).toBool()) {
        qDebug() << "[MAINWIN] 项目对话迁移已完成，跳过";
        return;
    }

    qDebug() << "[MAINWIN] 开始迁移旧项目对话...";

    // a) 从旧的项目目录 {projectPath}/.azur/data/chats/ 迁移
    const QJsonArray projHistory = s.value("projectHistory").toJsonArray();
    for (const QJsonValue &val : projHistory) {
        const QJsonObject entry = val.toObject();
        const QString projectPath = entry["path"].toString();
        if (projectPath.isEmpty()) continue;

        QString oldDataDir = QDir::toNativeSeparators(
            QDir::cleanPath(projectPath) + "/.azur/data");
        QString oldChatsDir = oldDataDir + "/chats";

        // 检查旧目录是否存在
        QDir oldDir(oldChatsDir);
        if (!oldDir.exists()) continue;

        // 创建临时管理器读取旧对话
        ConversationManager *tmpMgr = new ConversationManager(this);
        if (!tmpMgr->initialize(oldDataDir)) {
            qWarning() << "[MAINWIN] 无法读取旧项目对话目录:" << oldDataDir;
            delete tmpMgr;
            continue;
        }

        // 迁移每条旧对话
        const QJsonArray oldMeta = tmpMgr->conversationsMeta();
        for (const QJsonValue &mv : oldMeta) {
            QJsonObject metaObj = mv.toObject();
            QString convId = metaObj["id"].toString();
            QString title = metaObj["title"].toString();
            QJsonArray messages = tmpMgr->loadConversation(convId);
            if (!messages.isEmpty()) {
                projectConvMgr_->saveConversation(convId, messages, title, projectPath);
                qDebug() << "[MAINWIN] 从项目目录迁移对话:" << convId << "->" << projectPath;
            }
        }
        delete tmpMgr;
    }

    // b) 从全局管理器中的项目对话迁移
    const QJsonArray chatMeta = conversationManager_->conversationsMeta();
    // 构建项目路径集合用于判断
    QSet<QString> projectPaths;
    for (const QJsonValue &val : projHistory) {
        projectPaths.insert(val.toObject()["path"].toString());
    }

    for (const QJsonValue &mv : chatMeta) {
        QJsonObject metaObj = mv.toObject();
        QString convId = metaObj["id"].toString();
        QString title = metaObj["title"].toString();

        // 检查此对话 ID 是否在项目历史记录中
        for (const QJsonValue &val : projHistory) {
            QJsonObject entry = val.toObject();
            if (entry["conversationId"].toString() == convId) {
                QString projectPath = entry["path"].toString();
                QJsonArray messages = conversationManager_->loadConversation(convId);
                if (!messages.isEmpty()) {
                    projectConvMgr_->saveConversation(convId, messages, title, projectPath);
                    qDebug() << "[MAINWIN] 从全局管理器迁移项目对话:" << convId << "->" << projectPath;
                }
                // 从全局管理器中删除
                conversationManager_->deleteConversation(convId);
                break;
            }
        }
    }

    s.setValue("projectConvMigrationDone", true);
    qDebug() << "[MAINWIN] 项目对话迁移完成";
}
