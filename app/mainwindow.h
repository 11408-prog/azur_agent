#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <ElaWindow.h>
#include <ElaScrollPage.h>
#include <ElaScrollArea.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaListView.h>
#include <ElaMessageBar.h>
#include <ElaTheme.h>
#include <ElaLineEdit.h>
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaIcon.h>

#include <QSlider>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QCloseEvent>
#include <QSettings>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextBrowser>
#include <QDateTime>
#include <QFile>
#include <QMenu>
#include <QTimer>
#include <QElapsedTimer>
#include <QUrl>

#include "core/ai_client.h"
#include "data/conversationmanager.h"
#include "core/agent_engine.h"

class ChatPageWidget;
class SettingPageWidget;

class MainWindow : public ElaWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void onNewConversation();
    void loadConversation(const QString &id);

    void onSendClicked();
    void onApiChunkReceived(const QString &delta);
    void onApiResponseCompleted(const QString &fullText);
    void onApiError(const QString &errorMessage);

    void onGlobalHotkeyTriggered();

private:

#ifdef Q_OS_WIN
    static constexpr UINT HOTKEY_ID = 1;      // 热键唯一 ID
    bool registerHotkey();                    // 注册热键
    void unregisterHotkey();                  // 注销热键
#endif
    // ---- UI 构建 ----
    void setupNavigation();
    void setupAboutPage();
    void saveSettings();
    void loadSettings();

    // ---- Prompt 构建 ----
    QString buildSystemPrompt() const;

    // ---- AppBar 按钮更新 ----
    void updateToggleButtonState();

    // 侧边栏折叠按钮对应的导航栏 footer 节点 key（见 setupNavigation()）
    QString sidebarToggleFooterKey_;

    // ---- 导航页 ----
    ElaScrollPage *chatPage_;
    ElaScrollPage *settingPage_;
    ElaScrollPage *aboutPage_;

    // ---- 核心组件 ----
    DeepSeekClient *client_;
    AgentEngine *chatEngine_;
    ConversationManager *conversationManager_;

    // ---- UI 组件 ----
    ChatPageWidget *chatPageWidget_;
    SettingPageWidget *settingsPageWidget_;

    // ---- AppBar 按钮 ----
    // 注：这里刻意不放"侧边栏折叠"这类控件，因为它一旦占据 AppBar 的
    // LeftArea/RightArea，会把标题栏原本可拖动窗口的空白区域吃掉
    // （ElaAppBar 默认支持拖动窗口，但自定义控件区域需要额外接入
    // hit-test 机制才能把空白部分让出来）。这个按钮改放到导航栏的
    // footer 节点里（见 setupNavigation() 里的 sidebarToggleFooterKey_），
    // 导航栏区域本来就不属于标题栏拖动判定范围，不会有这个问题。

    // ---- Chat 模式状态 ----
    QList<QJsonObject> messageHistory_;
    QString lastUserMessage_;
    bool isWaitingResponse_ = false;
    QString currentConversationId_;
    QString systemPrompt_;
};

#endif // MAINWINDOW_H
