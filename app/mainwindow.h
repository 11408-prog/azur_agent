#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <ElaWindow.h>
#include <ElaIconButton.h>
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

private slots:
    void onNewConversation();
    void loadConversation(const QString &id);

    void onSendClicked();
    void onApiChunkReceived(const QString &delta);
    void onApiResponseCompleted(const QString &fullText);
    void onApiError(const QString &errorMessage);

private:
    // ---- UI 构建 ----
    void setupNavigation();
    void setupAboutPage();
    void saveSettings();
    void loadSettings();

    // ---- Prompt 构建 ----
    QString buildSystemPrompt() const;

    // ---- AppBar 按钮更新 ----
    void updateToggleButtonState();

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
    ElaIconButton *sidebarToggleBtn_;

    // ---- Chat 模式状态 ----
    QList<QJsonObject> messageHistory_;
    QString lastUserMessage_;
    bool isWaitingResponse_ = false;
    QString currentConversationId_;
    QString systemPrompt_;
};

#endif // MAINWINDOW_H
