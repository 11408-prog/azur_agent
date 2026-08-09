#ifndef SETTINGPAGEWIDGET_H
#define SETTINGPAGEWIDGET_H

#include <QWidget>
#include <QStringList>

class ElaLineEdit;
class ElaComboBox;
class QSlider;
class ElaPushButton;
class ElaText;
class QCheckBox;

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
    // 记住一个用过的模型名：加入"最近使用"建议列表
    void rememberModel(const QString &model);
    void loadSettings();
    void saveSettings();

    // ---- 主题 ----
    void syncThemeCombo(int mode);  // 供 MainWindow 在 themeModeChanged 时同步下拉框
    void applyTheme();              // 重刷卡片/分隔线/滑块/checkbox 的浅深两套 QSS

signals:
    void statusBarVisibilityChanged(bool vivible);
    void bgOpacityChanged(int value);
    void chatBgVisibilityChanged(bool enabled);
    void connectionTestRequested(const QString &apiKey, const QString &baseUrl);
    void themeModeChanged(int mode);   // 0=浅色, 1=深色, 2=跟随系统

private:
    void setupUI();

    ElaLineEdit *apiKeyEdit_;
    ElaLineEdit *baseUrlEdit_;
    ElaComboBox *modelComboBox_;
    ElaComboBox *themeCombo_ = nullptr;
    QSlider *bgOpacitySlider_;
    QLabel *bgValueLabel_ = nullptr;
    QList<QCheckBox *> checkBoxes_;
    QStringList recentModels_;
};

#endif // SETTINGPAGEWIDGET_H
