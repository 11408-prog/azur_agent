#include "app/settingpagewidget.h"
#include "data/appsettings.h"

#include <ElaLineEdit.h>
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaMessageBar.h>
#include <ElaIcon.h>
#include <ElaIconButton.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QUrl>
#include <QDebug>
#include <QFrame>
#include <QCheckBox>

SettingPageWidget::SettingPageWidget(QWidget *parent)
    : QWidget(parent)
    , chatApiKeyEdit_(nullptr)
    , chatBaseUrlEdit_(nullptr)
    , projectApiKeyEdit_(nullptr)
    , projectBaseUrlEdit_(nullptr)
    , chatModelComboBox_(nullptr)
    , projectModelComboBox_(nullptr)
    , bgOpacitySlider_(nullptr)
{
    setupUI();
}

SettingPageWidget::~SettingPageWidget() = default;

QString SettingPageWidget::chatApiKey() const
{
    return chatApiKeyEdit_ ? chatApiKeyEdit_->text().trimmed() : QString();
}

QString SettingPageWidget::chatBaseUrl() const
{
    return chatBaseUrlEdit_ ? chatBaseUrlEdit_->text().trimmed() : QString();
}

QString SettingPageWidget::projectApiKey() const
{
    return projectApiKeyEdit_ ? projectApiKeyEdit_->text().trimmed() : QString();
}

QString SettingPageWidget::projectBaseUrl() const
{
    return projectBaseUrlEdit_ ? projectBaseUrlEdit_->text().trimmed() : QString();
}

QString SettingPageWidget::chatModelName() const
{
    return chatModelComboBox_ ? chatModelComboBox_->currentText().trimmed() : QString();
}

QString SettingPageWidget::projectModelName() const
{
    return projectModelComboBox_ ? projectModelComboBox_->currentText().trimmed() : QString();
}

int SettingPageWidget::bgOpacity() const
{
    return bgOpacitySlider_ ? bgOpacitySlider_->value() : 25;
}

QStringList SettingPageWidget::recentModels() const
{
    return recentModels_;
}

void SettingPageWidget::setRecentModels(const QStringList &models)
{
    recentModels_ = models;
}

void SettingPageWidget::rememberModel(const QString &model)
{
    if (model.isEmpty()) return;
    recentModels_.removeAll(model);
    recentModels_.prepend(model);
    while (recentModels_.size() > 8) {
        recentModels_.removeLast();
    }
    if (chatModelComboBox_ && chatModelComboBox_->findText(model) == -1) {
        chatModelComboBox_->addItem(model);
    }
    if (projectModelComboBox_ && projectModelComboBox_->findText(model) == -1) {
        projectModelComboBox_->addItem(model);
    }
    saveSettings();
}

// 辅助：创建卡片容器（QFrame + 圆角阴影样式）
static QFrame* createCard(QWidget *parent)
{
    QFrame *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("SettingCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(
        "QFrame#SettingCard {"
        "  background-color: rgba(255, 255, 255, 0.82);"
        "  border: 1px solid rgba(0, 0, 0, 0.05);"
        "  border-radius: 12px;"
        "}"
        );
    return card;
}

// 辅助：创建卡片标题区（图标 + 标题 + 描述）
static QHBoxLayout* createCardHeader(QWidget *parent, ElaIconType::IconName iconName,
                                     const QString &title, const QString &desc)
{
    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(10);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    ElaIconButton *iconBtn = new ElaIconButton(iconName, 16, 28, 28, parent);
    iconBtn->setEnabled(false);

    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setSpacing(1);
    textLayout->setContentsMargins(0, 0, 0, 0);

    ElaText *titleText = new ElaText(title, parent);
    titleText->setTextPixelSize(14);
    QFont boldFont = titleText->font();
    boldFont.setBold(true);
    titleText->setFont(boldFont);

    ElaText *descText = new ElaText(desc, parent);
    descText->setTextPixelSize(11);
    descText->setStyleSheet("color: #888888;");

    textLayout->addWidget(titleText);
    textLayout->addWidget(descText);

    headerLayout->addWidget(iconBtn, 0, Qt::AlignTop);
    headerLayout->addLayout(textLayout, 1);

    return headerLayout;
}

// 辅助：创建水平分隔线
static QFrame* createSeparator(QWidget *parent)
{
    QFrame *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #e0e0e0;");
    line->setFixedHeight(1);
    return line;
}

// 辅助：创建一张完整的"连接配置"卡片（API Key + Base URL/测试连接 + 模型）。
// 聊天模式和项目模式各自需要一份结构完全一样的连接配置，用这个函数避免写两份重复代码；
// 通过输出参数把创建好的控件指针传出去，供 SettingPageWidget 保存到成员变量里。
static QFrame* createConnectionCard(QWidget *rootParent, SettingPageWidget *self,
                                     const QString &title, const QString &desc,
                                     ElaLineEdit **apiKeyEditOut,
                                     ElaLineEdit **baseUrlEditOut,
                                     ElaComboBox **modelComboOut)
{
    QFrame *card = createCard(rootParent);
    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 16, 20, 16);
    cardLayout->setSpacing(12);

    cardLayout->addLayout(createCardHeader(card, ElaIconType::Globe, title, desc));
    cardLayout->addWidget(createSeparator(card));

    QGridLayout *form = new QGridLayout();
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);
    form->setColumnStretch(0, 0);
    form->setColumnStretch(1, 1);

    // API Key
    ElaText *apiLabel = new ElaText("API Key", card);
    apiLabel->setTextPixelSize(13);
    apiLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ElaLineEdit *apiKeyEdit = new ElaLineEdit(card);
    apiKeyEdit->setPlaceholderText("请输入你的 API Key");
    apiKeyEdit->setEchoMode(QLineEdit::Password);
    apiKeyEdit->setMinimumHeight(34);
    QObject::connect(apiKeyEdit, &QLineEdit::editingFinished, self, &SettingPageWidget::saveSettings);
    form->addWidget(apiLabel, 0, 0);
    form->addWidget(apiKeyEdit, 0, 1);

    // Base URL + 测试连接
    ElaText *baseUrlLabel = new ElaText("Base URL", card);
    baseUrlLabel->setTextPixelSize(13);
    baseUrlLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ElaLineEdit *baseUrlEdit = new ElaLineEdit(card);
    baseUrlEdit->setPlaceholderText("例如 https://api.deepseek.com 或 https://api.openai.com/v1");
    baseUrlEdit->setMinimumHeight(34);
    QObject::connect(baseUrlEdit, &QLineEdit::editingFinished, self, &SettingPageWidget::saveSettings);

    ElaPushButton *testConnectionBtn = new ElaPushButton("测试连接", card);
    testConnectionBtn->setFixedSize(90, 30);
    QObject::connect(testConnectionBtn, &ElaPushButton::clicked, self, [self, apiKeyEdit, baseUrlEdit]() {
        const QString key = apiKeyEdit->text().trimmed();
        const QString url = baseUrlEdit->text().trimmed();
        const QUrl parsedUrl(url);
        if (key.isEmpty()) {
            ElaMessageBar::warning(ElaMessageBarType::TopRight, "提示", "请先填写 API Key", 3000);
            return;
        }
        if (!parsedUrl.isValid() || parsedUrl.host().isEmpty()
            || (parsedUrl.scheme() != "http" && parsedUrl.scheme() != "https")) {
            ElaMessageBar::warning(ElaMessageBarType::TopRight, "提示",
                                   "请填写有效的 http(s) Base URL", 3000);
            return;
        }
        self->saveSettings();
        emit self->connectionTestRequested(key, url);
    });

    QHBoxLayout *urlRow = new QHBoxLayout();
    urlRow->setSpacing(8);
    urlRow->setContentsMargins(0, 0, 0, 0);
    urlRow->addWidget(baseUrlEdit, 1);
    urlRow->addWidget(testConnectionBtn);
    form->addWidget(baseUrlLabel, 1, 0);
    form->addLayout(urlRow, 1, 1);

    // 模型
    ElaText *modelLabel = new ElaText("模型名称", card);
    modelLabel->setTextPixelSize(13);
    modelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ElaComboBox *modelCombo = new ElaComboBox(card);
    modelCombo->setEditable(true);
    modelCombo->setMinimumHeight(34);
    modelCombo->addItems({"deepseek-v4-flash", "deepseek-v4-pro"});
    QObject::connect(modelCombo, &QComboBox::currentTextChanged, self, [self](const QString &) {
        self->saveSettings();
    });
    form->addWidget(modelLabel, 2, 0);
    form->addWidget(modelCombo, 2, 1);

    cardLayout->addLayout(form);

    *apiKeyEditOut = apiKeyEdit;
    *baseUrlEditOut = baseUrlEdit;
    *modelComboOut = modelCombo;
    return card;
}

void SettingPageWidget::setupUI()
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(24, 24, 24, 24);
    rootLayout->setSpacing(16);

    // ============================================================
    // 卡片 1：聊天模式连接配置
    // ============================================================
    QFrame *chatConnCard = createConnectionCard(
        this, this, "聊天模式连接", "聊天模式使用的 AI 服务接入方式",
        &chatApiKeyEdit_, &chatBaseUrlEdit_, &chatModelComboBox_);
    rootLayout->addWidget(chatConnCard);

    // ============================================================
    // 卡片 2：项目模式连接配置
    // ============================================================
    QFrame *projectConnCard = createConnectionCard(
        this, this, "项目模式连接", "项目模式使用的 AI 服务接入方式",
        &projectApiKeyEdit_, &projectBaseUrlEdit_, &projectModelComboBox_);
    rootLayout->addWidget(projectConnCard);

    // ============================================================
    // 卡片 2：Agent 行为
    // ============================================================
    QFrame *agentCard = createCard(this);
    QVBoxLayout *agentLayout = new QVBoxLayout(agentCard);
    agentLayout->setContentsMargins(20, 16, 20, 16);
    agentLayout->setSpacing(12);

    agentLayout->addLayout(createCardHeader(agentCard, ElaIconType::Gear,
                                            "Agent 行为", "控制 Agent 的自动化程度与权限"));
    agentLayout->addWidget(createSeparator(agentCard));

    QGridLayout *agentForm = new QGridLayout();
    agentForm->setHorizontalSpacing(14);
    agentForm->setVerticalSpacing(12);
    agentForm->setColumnStretch(0, 0);
    agentForm->setColumnStretch(1, 1);

    // 默认模式
    ElaText *modeLabel = new ElaText("默认模式", agentCard);
    modeLabel->setTextPixelSize(13);
    modeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ElaComboBox *modeCombo = new ElaComboBox(agentCard);
    modeCombo->setMinimumHeight(34);
    modeCombo->addItems({"聊天模式", "项目模式"});
    modeCombo->setCurrentIndex(AppSettings::startupMode());
    connect(modeCombo, &QComboBox::currentIndexChanged, this, [](int idx) {
        AppSettings::setStartupMode(idx);
    });
    agentForm->addWidget(modeLabel, 0, 0);
    agentForm->addWidget(modeCombo, 0, 1);

    // Agent 权限
    ElaText *permLabel = new ElaText("Agent 权限", agentCard);
    permLabel->setTextPixelSize(13);
    permLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ElaComboBox *permCombo = new ElaComboBox(agentCard);
    permCombo->setMinimumHeight(34);
    permCombo->addItems({"每次确认", "自动执行"});
    permCombo->setCurrentIndex(AppSettings::agentPermission());
    connect(permCombo, &QComboBox::currentIndexChanged, this, [](int idx) {
        AppSettings::setAgentPermission(idx);
    });
    agentForm->addWidget(permLabel, 1, 0);
    agentForm->addWidget(permCombo, 1, 1);

    //Prompt 模式
    ElaText *promptLabel = new ElaText("Prompt 模式", agentCard);
    promptLabel->setTextPixelSize(13);
    promptLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ElaComboBox *promptCombo = new ElaComboBox(agentCard);
    promptCombo->setMinimumHeight(34);
    promptCombo->addItems({"精简版（本地模型推荐）", "完整版（云端模型推荐）"});
    promptCombo->setCurrentIndex(AppSettings::chatPromptMode());
    connect(promptCombo, &QComboBox::currentIndexChanged, this, [](int idx) {
        AppSettings::setChatPromptMode(idx);
    });
    agentForm->addWidget(promptLabel, 2, 0);
    agentForm->addWidget(promptCombo, 2, 1);

    agentLayout->addLayout(agentForm);
    rootLayout->addWidget(agentCard);

    // ============================================================
    // 卡片 3：界面与外观
    // ============================================================
    QFrame *uiCard = createCard(this);
    QVBoxLayout *uiLayout = new QVBoxLayout(uiCard);
    uiLayout->setContentsMargins(20, 16, 20, 16);
    uiLayout->setSpacing(12);

    uiLayout->addLayout(createCardHeader(uiCard, ElaIconType::Paintbrush,
                                         "界面与外观", "调整聊天区域的视觉效果"));
    uiLayout->addWidget(createSeparator(uiCard));

    QGridLayout *uiForm = new QGridLayout();
    uiForm->setHorizontalSpacing(14);
    uiForm->setVerticalSpacing(12);
    uiForm->setColumnStretch(0, 0);
    uiForm->setColumnStretch(1, 1);

    // 透明度
    ElaText *bgLabel = new ElaText("聊天背景透明度", uiCard);
    bgLabel->setTextPixelSize(13);
    bgLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    bgOpacitySlider_ = new QSlider(Qt::Horizontal, uiCard);
    bgOpacitySlider_->setRange(0, 100);
    bgOpacitySlider_->setValue(AppSettings::bgOpacity());
    bgOpacitySlider_->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  border-radius: 2px; height: 4px;"
        "  background: rgba(200,180,170,0.3);"
        "}"
        "QSlider::handle:horizontal {"
        "  background: rgba(200,140,120,0.85);"
        "  border: none; width: 14px; height: 14px;"
        "  margin: -5px 0; border-radius: 7px;"
        "}"
        "QSlider::handle:horizontal:hover {"
        "  background: rgba(210,150,130,0.95);"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: rgba(200,160,140,0.5); border-radius: 2px;"
        "}"
        );

    // 显示底部状态栏
    ElaText *statusLabel = new ElaText("显示底部状态栏", uiCard);
    statusLabel->setTextPixelSize(13);
    statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    QCheckBox *statusCheck = new QCheckBox(uiCard);
    statusCheck->setChecked(AppSettings::showStatusBar());
    statusCheck->setStyleSheet("QCheckBox { font-size: 13px; color: #3a3a4a; }"
                               "QCheckBox::indicator { width: 18px; height: 18px; }");
    connect(statusCheck, &QCheckBox::stateChanged, this, [this](int state) {
        bool visible = (state == Qt::Checked);
        AppSettings::setShowStatusBar(visible);
        emit statusBarVisibilityChanged(visible);
    });

    // 显示聊天背景（开关控制，默认关闭）
    ElaText *bgEnableLabel = new ElaText("显示聊天背景", uiCard);
    bgEnableLabel->setTextPixelSize(13);
    bgEnableLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    QCheckBox *bgEnableCheck = new QCheckBox(uiCard);
    bgEnableCheck->setChecked(AppSettings::chatBgEnabled());
    bgEnableCheck->setStyleSheet("QCheckBox { font-size: 13px; color: #3a3a4a; }"
                                 "QCheckBox::indicator { width: 18px; height: 18px; }");
    connect(bgEnableCheck, &QCheckBox::stateChanged, this, [this](int state) {
        bool enabled = (state == Qt::Checked);
        AppSettings::setChatBgEnabled(enabled);
        bgOpacitySlider_->setEnabled(enabled);
        emit chatBgVisibilityChanged(enabled);
    });
    bgOpacitySlider_->setEnabled(bgEnableCheck->isChecked());

    uiForm->addWidget(statusLabel, 1, 0);
    uiForm->addWidget(statusCheck, 1, 1);

    uiForm->addWidget(bgEnableLabel, 2, 0);
    uiForm->addWidget(bgEnableCheck, 2, 1);

    QLabel *bgValueLabel = new QLabel(uiCard);
    bgValueLabel->setStyleSheet("font-size: 13px; color: #8a8a9a;");
    bgValueLabel->setFixedWidth(40);
    bgValueLabel->setText(QString::number(bgOpacitySlider_->value()) + "%");

    connect(bgOpacitySlider_, &QSlider::valueChanged, this, [this, bgValueLabel](int val) {
        bgValueLabel->setText(QString::number(val) + "%");
        emit bgOpacityChanged(val);
        AppSettings::setBgOpacity(val);
    });

    QHBoxLayout *sliderRow = new QHBoxLayout();
    sliderRow->setSpacing(10);
    sliderRow->setContentsMargins(0, 0, 0, 0);
    sliderRow->addWidget(bgOpacitySlider_, 1);
    sliderRow->addWidget(bgValueLabel);

    uiForm->addWidget(bgLabel, 0, 0);
    uiForm->addLayout(sliderRow, 0, 1);

    uiLayout->addLayout(uiForm);
    rootLayout->addWidget(uiCard);

    rootLayout->addStretch();
}

void SettingPageWidget::loadSettings()
{
    qDebug() << "[SETTING] loadSettings";
    chatApiKeyEdit_->setText(AppSettings::chatApiKey());
    chatBaseUrlEdit_->setText(AppSettings::chatBaseUrl());
    projectApiKeyEdit_->setText(AppSettings::projectApiKey());
    projectBaseUrlEdit_->setText(AppSettings::projectBaseUrl());

    recentModels_ = AppSettings::recentModels();
    for (const QString &model : recentModels_) {
        if (model.isEmpty()) continue;
        if (chatModelComboBox_->findText(model) == -1) {
            chatModelComboBox_->addItem(model);
        }
        if (projectModelComboBox_->findText(model) == -1) {
            projectModelComboBox_->addItem(model);
        }
    }
    chatModelComboBox_->setCurrentText(AppSettings::chatModel());
    projectModelComboBox_->setCurrentText(AppSettings::projectModel());
}

void SettingPageWidget::saveSettings()
{
    qDebug() << "[SETTING] saveSettings";
    AppSettings::setChatApiKey(chatApiKeyEdit_->text());
    AppSettings::setChatBaseUrl(chatBaseUrlEdit_->text().trimmed());
    AppSettings::setProjectApiKey(projectApiKeyEdit_->text());
    AppSettings::setProjectBaseUrl(projectBaseUrlEdit_->text().trimmed());
    AppSettings::setChatModel(chatModelComboBox_->currentText());
    AppSettings::setProjectModel(projectModelComboBox_->currentText());
    AppSettings::setRecentModels(recentModels_);
}
