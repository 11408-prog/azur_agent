#ifndef SETTINGPAGEWIDGET_H
#define SETTINGPAGEWIDGET_H

#include <QWidget>
#include <QStringList>

class ElaLineEdit;
class ElaComboBox;
class QSlider;
class ElaPushButton;
class ElaText;

class SettingPageWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SettingPageWidget(QWidget *parent = nullptr);
    ~SettingPageWidget() override;

    // ---- Getters ----
    // 聊天模式和项目模式分别使用完全独立的连接配置（见 AppSettings::chatApiKey/projectApiKey 等）
    QString chatApiKey() const;
    QString chatBaseUrl() const;
    QString projectApiKey() const;
    QString projectBaseUrl() const;
    QString chatModelName() const;
    QString projectModelName() const;
    int bgOpacity() const;
    QStringList recentModels() const;

    // ---- 设置项管理 ----
    void setRecentModels(const QStringList &models);
    // 记住一个用过的模型名：加入"最近使用"建议列表，聊天/项目两个下拉框共享同一份建议
    void rememberModel(const QString &model);
    void loadSettings();
    void saveSettings();

signals:
    void statusBarVisibilityChanged(bool vivible);
    void bgOpacityChanged(int value);
    void chatBgVisibilityChanged(bool enabled);
    void connectionTestRequested(const QString &apiKey, const QString &baseUrl);

private:
    void setupUI();

    ElaLineEdit *chatApiKeyEdit_;
    ElaLineEdit *chatBaseUrlEdit_;
    ElaLineEdit *projectApiKeyEdit_;
    ElaLineEdit *projectBaseUrlEdit_;
    ElaComboBox *chatModelComboBox_;
    ElaComboBox *projectModelComboBox_;
    QSlider *bgOpacitySlider_;
    QStringList recentModels_;
};

#endif // SETTINGPAGEWIDGET_H
