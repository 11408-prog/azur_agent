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
    // 工作区根目录：read_file/list_directory 只能访问这个目录内的路径，空表示未启用工具调用
    QString workspaceRoot() const;

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
    // 试听当前音色：MainWindow 负责用 TtsClient 合成并播放
    void voicePreviewRequested(const QString &text, const QString &voice);

private:
    void setupUI();
    // 当前选中的音色 ID：选中预设项时取 itemData；在编辑框里手动输入了
    // 自定义音色 ID（currentData 无效）时取编辑文本
    QString selectedTtsVoice() const;

    ElaLineEdit *apiKeyEdit_;
    ElaLineEdit *baseUrlEdit_;
    ElaComboBox *modelComboBox_;
    ElaComboBox *themeCombo_ = nullptr;
    QSlider *bgOpacitySlider_;
    QLabel *bgValueLabel_ = nullptr;
    QList<QCheckBox *> checkBoxes_;
    QStringList recentModels_;

    // ---- 语音朗读 ----
    QCheckBox *ttsEnabledCheck_ = nullptr;
    ElaComboBox *ttsVoiceCombo_ = nullptr;

    // ---- 工具调用 ----
    ElaLineEdit *workspaceRootEdit_ = nullptr;

    // ---- 事实记忆（P3） ----
    QCheckBox *memoryEnabledCheck_ = nullptr;
};

#endif // SETTINGPAGEWIDGET_H
