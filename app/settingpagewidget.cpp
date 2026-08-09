#include "app/settingpagewidget.h"
#include "data/appsettings.h"
#include "ui/theme.h"

#include <ElaLineEdit.h>
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaMessageBar.h>
#include <ElaIcon.h>
#include <ElaIconButton.h>
#include <ElaTheme.h>

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
    , apiKeyEdit_(nullptr)
    , baseUrlEdit_(nullptr)
    , modelComboBox_(nullptr)
    , bgOpacitySlider_(nullptr)
{
    setupUI();
}

SettingPageWidget::~SettingPageWidget() = default;

QString SettingPageWidget::apiKey() const
{
    return apiKeyEdit_ ? apiKeyEdit_->text().trimmed() : QString();
}

QString SettingPageWidget::baseUrl() const
{
    return baseUrlEdit_ ? baseUrlEdit_->text().trimmed() : QString();
}

QString SettingPageWidget::modelName() const
{
    return modelComboBox_ ? modelComboBox_->currentText().trimmed() : QString();
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
    if (modelComboBox_ && modelComboBox_->findText(model) == -1) {
        modelComboBox_->addItem(model);
    }
    saveSettings();
}

// 辅助：创建卡片容器（QFrame + 圆角样式，背景走主题色板）
static QFrame* createCard(QWidget *parent)
{
    QFrame *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("SettingCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(
        QString("QFrame#SettingCard {"
                "  background-color: %1;"
                "  border: 1px solid %2;"
                "  border-radius: 12px;"
                "}")
            .arg(UiTheme::qss(UiTheme::surface()),
                 UiTheme::qss(UiTheme::border())));
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
    descText->setTextStyle(ElaTextType::Caption);

    textLayout->addWidget(titleText);
    textLayout->addWidget(descText);

    headerLayout->addWidget(iconBtn, 0, Qt::AlignTop);
    headerLayout->addLayout(textLayout, 1);

    return headerLayout;
}

// 辅助：创建水平分隔线（颜色由 applyTheme 统一刷成主题边框色）
static QFrame* createSeparator(QWidget *parent)
{
    QFrame *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("color: %1;").arg(UiTheme::qss(UiTheme::border())));
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
    // 卡片 1：AI 服务连接配置
    // ============================================================
    QFrame *connCard = createConnectionCard(
        this, this, "AI 服务连接", "与企业对话所使用的 AI 服务接入方式",
        &apiKeyEdit_, &baseUrlEdit_, &modelComboBox_);
    rootLayout->addWidget(connCard);

    // ============================================================
    // 卡片 2：对话设置
    // ============================================================
    QFrame *agentCard = createCard(this);
    QVBoxLayout *agentLayout = new QVBoxLayout(agentCard);
    agentLayout->setContentsMargins(20, 16, 20, 16);
    agentLayout->setSpacing(12);

    agentLayout->addLayout(createCardHeader(agentCard, ElaIconType::Gear,
                                            "对话设置", "控制系统提示词的详略程度"));
    agentLayout->addWidget(createSeparator(agentCard));

    QGridLayout *agentForm = new QGridLayout();
    agentForm->setHorizontalSpacing(14);
    agentForm->setVerticalSpacing(12);
    agentForm->setColumnStretch(0, 0);
    agentForm->setColumnStretch(1, 1);

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
    agentForm->addWidget(promptLabel, 0, 0);
    agentForm->addWidget(promptCombo, 0, 1);

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

    // 显示底部状态栏
    ElaText *statusLabel = new ElaText("显示底部状态栏", uiCard);
    statusLabel->setTextPixelSize(13);
    statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    QCheckBox *statusCheck = new QCheckBox(uiCard);
    statusCheck->setChecked(AppSettings::showStatusBar());
    checkBoxes_.append(statusCheck);
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
    checkBoxes_.append(bgEnableCheck);
    connect(bgEnableCheck, &QCheckBox::stateChanged, this, [this](int state) {
        bool enabled = (state == Qt::Checked);
        AppSettings::setChatBgEnabled(enabled);
        bgOpacitySlider_->setEnabled(enabled);
        emit chatBgVisibilityChanged(enabled);
    });
    bgOpacitySlider_->setEnabled(bgEnableCheck->isChecked());

    // 主题模式
    ElaText *themeLabel = new ElaText("主题模式", uiCard);
    themeLabel->setTextPixelSize(13);
    themeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    themeCombo_ = new ElaComboBox(uiCard);
    themeCombo_->setMinimumHeight(34);
    themeCombo_->addItems({"浅色", "深色", "跟随系统"});
    themeCombo_->setCurrentIndex(qBound(0, AppSettings::themeMode(), 2));
    connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
        emit themeModeChanged(qBound(0, idx, 2));
    });

    uiForm->addWidget(statusLabel, 1, 0);
    uiForm->addWidget(statusCheck, 1, 1);

    uiForm->addWidget(bgEnableLabel, 2, 0);
    uiForm->addWidget(bgEnableCheck, 2, 1);

    uiForm->addWidget(themeLabel, 3, 0);
    uiForm->addWidget(themeCombo_, 3, 1);

    bgValueLabel_ = new QLabel(uiCard);
    bgValueLabel_->setFixedWidth(40);
    bgValueLabel_->setText(QString::number(bgOpacitySlider_->value()) + "%");

    connect(bgOpacitySlider_, &QSlider::valueChanged, this, [this](int val) {
        bgValueLabel_->setText(QString::number(val) + "%");
        emit bgOpacityChanged(val);
        AppSettings::setBgOpacity(val);
    });

    QHBoxLayout *sliderRow = new QHBoxLayout();
    sliderRow->setSpacing(10);
    sliderRow->setContentsMargins(0, 0, 0, 0);
    sliderRow->addWidget(bgOpacitySlider_, 1);
    sliderRow->addWidget(bgValueLabel_);

    uiForm->addWidget(bgLabel, 0, 0);
    uiForm->addLayout(sliderRow, 0, 1);

    uiLayout->addLayout(uiForm);
    rootLayout->addWidget(uiCard);

    rootLayout->addStretch();

    // 主题切换时重刷自定义 QSS
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this](ElaThemeType::ThemeMode) {
        applyTheme();
    });
    applyTheme();
}

void SettingPageWidget::syncThemeCombo(int mode)
{
    if (!themeCombo_) return;
    QSignalBlocker blocker(themeCombo_);
    themeCombo_->setCurrentIndex(qBound(0, mode, 2));
}

void SettingPageWidget::applyTheme()
{
    // 卡片
    const QString cardQss = QString(
        "QFrame#SettingCard {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 12px;"
        "}")
        .arg(UiTheme::qss(UiTheme::surface()),
             UiTheme::qss(UiTheme::border()));
    const QList<QFrame *> cards = findChildren<QFrame *>(QStringLiteral("SettingCard"));
    for (QFrame *card : cards) {
        card->setStyleSheet(cardQss);
    }

    // 分隔线
    const QString lineQss = QString("color: %1;").arg(UiTheme::qss(UiTheme::border()));
    const QList<QFrame *> frames = findChildren<QFrame *>();
    for (QFrame *f : frames) {
        if (f->frameShape() == QFrame::HLine) {
            f->setStyleSheet(lineQss);
        }
    }

    // 滑块
    if (bgOpacitySlider_) {
        bgOpacitySlider_->setStyleSheet(QString(
            "QSlider::groove:horizontal {"
            "  border-radius: 2px; height: 4px; background: %1;"
            "}"
            "QSlider::handle:horizontal {"
            "  background: %2; border: none; width: 14px; height: 14px;"
            "  margin: -5px 0; border-radius: 7px;"
            "}"
            "QSlider::handle:horizontal:hover { background: %3; }"
            "QSlider::sub-page:horizontal { background: %4; border-radius: 2px; }"
            )
            .arg(UiTheme::qss(UiTheme::border()),
                 UiTheme::qss(UiTheme::accent()),
                 UiTheme::qss(UiTheme::accentHover()),
                 UiTheme::qss(UiTheme::accent(), 140)));
    }

    // checkbox
    const QString checkQss = QString(
        "QCheckBox { font-size: 13px; color: %1; }"
        "QCheckBox::indicator { width: 18px; height: 18px; }")
        .arg(UiTheme::qss(UiTheme::textPrimary()));
    for (QCheckBox *cb : checkBoxes_) {
        cb->setStyleSheet(checkQss);
    }

    // 数值标签
    if (bgValueLabel_) {
        bgValueLabel_->setStyleSheet(
            QString("font-size: 13px; color: %1;").arg(UiTheme::qss(UiTheme::textSecondary())));
    }
}

void SettingPageWidget::loadSettings()
{
    qDebug() << "[SETTING] loadSettings";
    apiKeyEdit_->setText(AppSettings::apiKey());
    baseUrlEdit_->setText(AppSettings::baseUrl());

    recentModels_ = AppSettings::recentModels();
    for (const QString &model : recentModels_) {
        if (model.isEmpty()) continue;
        if (modelComboBox_->findText(model) == -1) {
            modelComboBox_->addItem(model);
        }
    }
    modelComboBox_->setCurrentText(AppSettings::model());

    syncThemeCombo(AppSettings::themeMode());
    applyTheme();
}

void SettingPageWidget::saveSettings()
{
    qDebug() << "[SETTING] saveSettings";
    AppSettings::setApiKey(apiKeyEdit_->text());
    AppSettings::setBaseUrl(baseUrlEdit_->text().trimmed());
    AppSettings::setModel(modelComboBox_->currentText());
    AppSettings::setRecentModels(recentModels_);
}
