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

QString model()//读取模型
{
    return store().value("model","deepseek-v4-flash").toString().trimmed();
}
void setModel(const QString &model)
{
    store().setValue("model",model);
}

QStringList recentModels() { return store().value("recentModels").toStringList(); }
void setRecentModels(const QStringList &models) { store().setValue("recentModels", models); }

int themeMode() { return store().value("themeMode", 2).toInt(); }
void setThemeMode(int mode) { store().setValue("themeMode", mode); }

int bgOpacity() { return store().value("bgOpacity", 25).toInt(); }
void setBgOpacity(int opacity) { store().setValue("bgOpacity", opacity); }

bool chatBgEnabled() { return store().value("chatBgEnabled", false).toBool(); }
void setChatBgEnabled(bool enabled) { store().setValue("chatBgEnabled", enabled); }

bool showStatusBar(){return store().value("showStatusBar",true).toBool();}
void setShowStatusBar(bool show){store().setValue("showStatusBar",show);}

int chatPromptMode() { return store().value("chatPromptMode", 0).toInt(); }
void setChatPromptMode(int mode) { store().setValue("chatPromptMode", mode); }
}
