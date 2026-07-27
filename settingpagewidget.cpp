#include "settingpagewidget.h"

#include <ElaLineEdit.h>
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaMessageBar.h>
#include <ElaIcon.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSettings>
#include <QUrl>
#include <QDebug>

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

void SettingPageWidget::setupUI()
{
    QVBoxLayout *settingLayout = new QVBoxLayout(this);
    settingLayout->setContentsMargins(20, 20, 20, 20);
    settingLayout->setSpacing(16);

    // ---- 连接配置标题 ----
    ElaText *title = new ElaText("连接配置", this);
    title->setTextPixelSize(20);
    QFont boldFont = title->font();
    boldFont.setBold(true);
    title->setFont(boldFont);
    settingLayout->addWidget(title);

    // ---- API Key ----
    QWidget *apiRow = new QWidget(this);
    QHBoxLayout *apiLayout = new QHBoxLayout(apiRow);
    apiLayout->setContentsMargins(0, 0, 0, 0);
    ElaText *apiLabel = new ElaText("API Key", apiRow);
    apiLabel->setFixedWidth(100);
    apiKeyEdit_ = new ElaLineEdit(apiRow);
    apiKeyEdit_->setPlaceholderText("请输入你的 API Key");
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    connect(apiKeyEdit_, &QLineEdit::editingFinished, this, &SettingPageWidget::saveSettings);
    apiLayout->addWidget(apiLabel);
    apiLayout->addWidget(apiKeyEdit_, 1);
    settingLayout->addWidget(apiRow);

    // ---- Base URL ----
    QWidget *baseUrlRow = new QWidget(this);
    QHBoxLayout *baseUrlLayout = new QHBoxLayout(baseUrlRow);
    baseUrlLayout->setContentsMargins(0, 0, 0, 0);
    ElaText *baseUrlLabel = new ElaText("Base URL", baseUrlRow);
    baseUrlLabel->setFixedWidth(100);
    baseUrlEdit_ = new ElaLineEdit(baseUrlRow);
    baseUrlEdit_->setPlaceholderText("例如 https://api.deepseek.com 或 https://api.openai.com/v1");
    connect(baseUrlEdit_, &QLineEdit::editingFinished, this, &SettingPageWidget::saveSettings);
    ElaPushButton *testConnectionBtn = new ElaPushButton("测试连接", baseUrlRow);
    testConnectionBtn->setFixedSize(96, 32);
    connect(testConnectionBtn, &ElaPushButton::clicked, this, [this]() {
        const QString key = apiKeyEdit_->text().trimmed();
        const QString url = baseUrlEdit_->text().trimmed();
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
        saveSettings();
        emit connectionTestRequested(key, url);
    });
    baseUrlLayout->addWidget(baseUrlLabel);
    baseUrlLayout->addWidget(baseUrlEdit_, 1);
    baseUrlLayout->addWidget(testConnectionBtn);
    settingLayout->addWidget(baseUrlRow);

    // ---- 模型名称 ----
    QWidget *modelRow = new QWidget(this);
    QHBoxLayout *modelLayout = new QHBoxLayout(modelRow);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    ElaText *modelLabel = new ElaText("模型名称", modelRow);
    modelLabel->setFixedWidth(100);
    modelComboBox_ = new ElaComboBox(modelRow);
    modelComboBox_->setEditable(true);
    modelComboBox_->addItems({"deepseek-v4-flash", "deepseek-v4-pro"});
    connect(modelComboBox_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        saveSettings();
    });
    modelLayout->addWidget(modelLabel);
    modelLayout->addWidget(modelComboBox_, 1);
    settingLayout->addWidget(modelRow);

    // ---- 模式选择 ----
    QWidget *modeRow = new QWidget(this);
    QHBoxLayout *modeLayout = new QHBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    ElaText *modeLabel = new ElaText("默认模式", modeRow);
    modeLabel->setFixedWidth(100);
    ElaComboBox *modeCombo = new ElaComboBox(modeRow);
    modeCombo->addItems({"聊天模式", "项目模式"});
    {
        QSettings s("AzurStudio", "AzurAgent");
        modeCombo->setCurrentIndex(s.value("startupMode", 0).toInt());
    }
    connect(modeCombo, &QComboBox::currentIndexChanged, this, [](int idx) {
        QSettings settings("AzurStudio", "AzurAgent");
        settings.setValue("startupMode", idx);
    });
    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(modeCombo, 1);
    settingLayout->addWidget(modeRow);

    // ---- Agent权限选择 ----
    QWidget *permRow = new QWidget(this);
    QHBoxLayout *permLayout = new QHBoxLayout(permRow);
    permLayout->setContentsMargins(0, 0, 0, 0);
    ElaText *permLabel = new ElaText("Agent权限", permRow);
    permLabel->setFixedWidth(100);
    ElaComboBox *permCombo = new ElaComboBox(permRow);
    permCombo->addItems({"每次确认", "自动执行"});
    {
        QSettings s("AzurStudio", "AzurAgent");
        permCombo->setCurrentIndex(s.value("agentPermission", 0).toInt());
    }
    connect(permCombo, &QComboBox::currentIndexChanged, this, [](int idx) {
        QSettings settings("AzurStudio", "AzurAgent");
        settings.setValue("agentPermission", idx);
    });
    permLayout->addWidget(permLabel);
    permLayout->addWidget(permCombo, 1);
    settingLayout->addWidget(permRow);

    // ---- 聊天背景 ----
    ElaText *bgTitle = new ElaText("聊天背景", this);
    bgTitle->setTextPixelSize(20);
    QFont bgBold = bgTitle->font();
    bgBold.setBold(true);
    bgTitle->setFont(bgBold);
    settingLayout->addWidget(bgTitle);

    QWidget *bgRow = new QWidget(this);
    QHBoxLayout *bgRowLayout = new QHBoxLayout(bgRow);
    bgRowLayout->setContentsMargins(0, 0, 0, 0);
    bgRowLayout->setSpacing(10);

    ElaText *bgLabel = new ElaText("透明度", bgRow);
    bgLabel->setFixedWidth(100);

    bgOpacitySlider_ = new QSlider(Qt::Horizontal, bgRow);
    bgOpacitySlider_->setRange(0, 100);
    {
        QSettings s("AzurStudio", "AzurAgent");
        bgOpacitySlider_->setValue(s.value("bgOpacity", 25).toInt());
    }
    bgOpacitySlider_->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  border-radius: 2px; height: 4px; background: rgba(0,0,0,0.12);"
        "}"
        "QSlider::handle:horizontal {"
        "  background: #bbb; border: none; width: 14px; height: 14px;"
        "  margin: -5px 0; border-radius: 7px;"
        "}"
        "QSlider::handle:horizontal:hover { background: #999; }"
        "QSlider::sub-page:horizontal {"
        "  background: rgba(74,158,255,0.4); border-radius: 2px;"
        "}"
    );

    QLabel *bgValueLabel = new QLabel(bgRow);
    bgValueLabel->setStyleSheet("font-size: 13px; color: #666;");
    bgValueLabel->setFixedWidth(36);
    bgValueLabel->setText(QString::number(bgOpacitySlider_->value()) + "%");

    connect(bgOpacitySlider_, &QSlider::valueChanged, this, [this, bgValueLabel](int val) {
        bgValueLabel->setText(QString::number(val) + "%");
        emit bgOpacityChanged(val);
        QSettings s("AzurStudio", "AzurAgent");
        s.setValue("bgOpacity", val);
    });

    bgRowLayout->addWidget(bgLabel);
    bgRowLayout->addWidget(bgOpacitySlider_, 1);
    bgRowLayout->addWidget(bgValueLabel);
    settingLayout->addWidget(bgRow);

    settingLayout->addStretch();
}

void SettingPageWidget::loadSettings()
{
    qDebug() << "[SETTING] loadSettings";
    QSettings settings("AzurStudio", "AzurAgent");
    apiKeyEdit_->setText(settings.value("apiKey").toString());
    baseUrlEdit_->setText(settings.value("baseUrl", "https://api.deepseek.com").toString());
    recentModels_ = settings.value("recentModels").toStringList();
    for (const QString &model : recentModels_) {
        if (!model.isEmpty() && modelComboBox_->findText(model) == -1) {
            modelComboBox_->addItem(model);
        }
    }
    modelComboBox_->setCurrentText(settings.value("model", "deepseek-v4-flash").toString());
}

void SettingPageWidget::saveSettings()
{
    qDebug() << "[SETTING] saveSettings";
    QSettings settings("AzurStudio", "AzurAgent");
    settings.setValue("apiKey", apiKeyEdit_->text());
    settings.setValue("baseUrl", baseUrlEdit_->text().trimmed());
    settings.setValue("model", modelComboBox_->currentText());
    settings.setValue("recentModels", recentModels_);
}
