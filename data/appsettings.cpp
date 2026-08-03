#include "data/appsettings.h"

#include <QSettings>

namespace
{
QSettings &store()
{
static QSettings s("AzurStudio","AzurAgent");
    return s;
}
}

namespace AppSettings{

QString apiKey()//读取api
{
    return store().value("apiKey").toString().trimmed();
}
void setApiKey(const QString &key)//保存api
{
    store().setValue("apiKey",key);
}

QString baseUrl()//读取网址
{
    return store().value("baseUrl","https://api.deepseek.com").toString().trimmed();
}
void setBaseUrl(const QString &url)
{
    store().setValue("baseUrl",url);
}

QString chatApiKey()//读取聊天模式api key
{
    const QString v = store().value("chatApiKey").toString().trimmed();
    return v.isEmpty() ? apiKey() : v;
}
void setChatApiKey(const QString &key)
{
    store().setValue("chatApiKey",key);
}

QString chatBaseUrl()//读取聊天模式网址
{
    const QString v = store().value("chatBaseUrl").toString().trimmed();
    return v.isEmpty() ? baseUrl() : v;
}
void setChatBaseUrl(const QString &url)
{
    store().setValue("chatBaseUrl",url);
}

QString projectApiKey()//读取项目模式api key
{
    const QString v = store().value("projectApiKey").toString().trimmed();
    return v.isEmpty() ? apiKey() : v;
}
void setProjectApiKey(const QString &key)
{
    store().setValue("projectApiKey",key);
}

QString projectBaseUrl()//读取项目模式网址
{
    const QString v = store().value("projectBaseUrl").toString().trimmed();
    return v.isEmpty() ? baseUrl() : v;
}
void setProjectBaseUrl(const QString &url)
{
    store().setValue("projectBaseUrl",url);
}

QString model()//读取模型
{
    return store().value("model","deepseek-v4-flash").toString().trimmed();
}
void setModel(const QString &model)
{
    store().setValue("model",model);
}

QString chatModel()//读取聊天模式模型
{
    const QString v = store().value("chatModel").toString().trimmed();
    // 专属字段还没设置过（比如从旧版本升级上来）时，退回旧的单一 model 设置，
    // 避免用户升级后发现模型名称突然空了
    return v.isEmpty() ? model() : v;
}
void setChatModel(const QString &model)
{
    store().setValue("chatModel",model);
}

QString projectModel()//读取项目模式模型
{
    const QString v = store().value("projectModel").toString().trimmed();
    return v.isEmpty() ? model() : v;
}
void setProjectModel(const QString &model)
{
    store().setValue("projectModel",model);
}

QStringList recentModels() { return store().value("recentModels").toStringList(); }
void setRecentModels(const QStringList &models) { store().setValue("recentModels", models); }

int agentPermission() { return store().value("agentPermission", 0).toInt(); }
void setAgentPermission(int mode) { store().setValue("agentPermission", mode); }

int startupMode() { return store().value("startupMode", 0).toInt(); }
void setStartupMode(int mode) { store().setValue("startupMode", mode); }

int bgOpacity() { return store().value("bgOpacity", 25).toInt(); }
void setBgOpacity(int opacity) { store().setValue("bgOpacity", opacity); }

bool chatBgEnabled() { return store().value("chatBgEnabled", false).toBool(); }
void setChatBgEnabled(bool enabled) { store().setValue("chatBgEnabled", enabled); }

bool showStatusBar(){return store().value("showStatusBar",true).toBool();}
void setShowStatusBar(bool show){store().setValue("showStatusBar",show);}

bool projectLeftPanelCollapsed() { return store().value("projectLeftPanelCollapsed", false).toBool(); }
void setProjectLeftPanelCollapsed(bool collapsed) { store().setValue("projectLeftPanelCollapsed", collapsed); }

bool projectRightPanelCollapsed() { return store().value("projectRightPanelCollapsed", false).toBool(); }
void setProjectRightPanelCollapsed(bool collapsed) { store().setValue("projectRightPanelCollapsed", collapsed); }

QString lastProjectPath() { return store().value("lastProjectPath").toString(); }
void setLastProjectPath(const QString &path) { store().setValue("lastProjectPath", path); }

QJsonArray projectHistory() { return store().value("projectHistory").toJsonArray(); }
void setProjectHistory(const QJsonArray &history) { store().setValue("projectHistory", history); }

bool projectConvMigrationDone() { return store().value("projectConvMigrationDone", false).toBool(); }
void setProjectConvMigrationDone(bool done) { store().setValue("projectConvMigrationDone", done); }

int chatPromptMode() { return store().value("chatPromptMode", 0).toInt(); }
void setChatPromptMode(int mode) { store().setValue("chatPromptMode", mode); }
}
