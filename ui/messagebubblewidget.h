#ifndef MESSAGEBUBBLEWIDGET_H
#define MESSAGEBUBBLEWIDGET_H

#include <QWidget>
#include <QString>
#include <QStringList>

class QLabel;
class QVBoxLayout;
class QTextBrowser;
class QFrame;
class ElaText;
class QTimer;

class MessageBubbleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MessageBubbleWidget(bool isUser, QWidget *parent = nullptr);
    ~MessageBubbleWidget() override;

    // ---- 用户消息（纯文本） ----
    void setUserContent(const QString &text);

    // ---- AI 消息（Markdown 渲染） ----
    void setAiContent(const QString &markdown);
    void setAiStreamingContent(const QString &plainText);
    QTextBrowser *aiContentBrowser() const;

    // ---- 步骤指示器（仅 Chat 模式使用） ----
    void enableStepIndicator(bool enable);
    bool isStepIndicatorEnabled() const;
    void updateStep(const QString &text);
    void finishStep(bool success, const QString &finalText);
    void spinnerTick(int frame);

    // ---- 内容占位动画（生成前显示的旋转 ⠋） ----
    void startContentSpinner();
    void stopContentSpinner();

    // ---- 时间戳 ----
    void setTimestamp(const QString &ts);

    // ---- 全局头像目录配置 ----
    static void setAvatarDirectory(const QString &dir);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void initUI();
    void applyTheme();
    QLabel *createAvatar();
    void createStepIndicator();
    void updateBubbleMaxWidth();
    void renderMarkdown(const QString &markdown);

    bool isUser_;
    QLabel *avatar_;
    // 气泡容器：之前是 ElaScrollPageArea，改成普通 QFrame。
    // 原因：ElaScrollPageArea 的背景色是从 ElaTheme 全局主题色板里读的，
    // 同一个类的所有实例只能是同一个颜色——没法让"用户气泡"和"AI气泡"
    // 分别显示不同颜色。QFrame + WA_StyledBackground 才能让每个实例的
    // QSS 背景色真正生效。
    QFrame *bubble_;
    QVBoxLayout *bubbleLayout_;
    QTextBrowser *contentBrowser_;
    ElaText *userText_;
    QLabel *timeLabel_;

    // 步骤指示器
    QWidget *stepRow_ = nullptr;
    QLabel *stepIcon_ = nullptr;
    QLabel *stepText_ = nullptr;
    bool stepIndicatorEnabled_ = false;

    static QString s_avatarDir;

    // ---- 内容旋转动画 ----
    QTimer *contentSpinnerTimer_ = nullptr;
    int contentSpinnerFrame_ = 0;

    // ---- 大文本惰性渲染 ----
    static constexpr int kLazyRenderThreshold = 2000;
    static constexpr int kLazyRenderInitialLines = 100;
    QString fullMarkdown_;
    void renderFullContent();

    // 最近一次 setAiContent 的 Markdown 原文：主题切换时据此重渲染（深色下颜色才会刷新）
    QString currentMarkdown_;
};

#endif // MESSAGEBUBBLEWIDGET_H
