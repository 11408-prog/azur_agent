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
    QString apiKey() const;
    QString baseUrl() const;
    QString modelName() const;
    int bgOpacity() const;
    QStringList recentModels() const;

    // ---- 设置项管理 ----
    void setRecentModels(const QStringList &models);
    void rememberModel(const QString &model);
    void loadSettings();
    void saveSettings();

signals:
    void bgOpacityChanged(int value);
    void connectionTestRequested(const QString &apiKey, const QString &baseUrl);

private:
    void setupUI();

    ElaLineEdit *apiKeyEdit_;
    ElaLineEdit *baseUrlEdit_;
    ElaComboBox *modelComboBox_;
    QSlider *bgOpacitySlider_;
    QStringList recentModels_;
};

#endif // SETTINGPAGEWIDGET_H
