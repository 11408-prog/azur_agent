#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QString>
#include <QStringList>
#include <QJsonArray>

// 统一封装项目里所有 QSettings("AzurStudio", "AzurAgent") 的读写。
// 改 key 名字/默认值只需要改这一处。
namespace AppSettings {

// ---- 连接配置 ----
QString apiKey();
void setApiKey(const QString &key);

QString baseUrl();
void setBaseUrl(const QString &url);

QString model();
void setModel(const QString &model);

QStringList recentModels();
void setRecentModels(const QStringList &models);

// ---- UI ----
int bgOpacity();
void setBgOpacity(int opacity);

bool chatBgEnabled();
void setChatBgEnabled(bool enabled);

bool showStatusBar();
void setShowStatusBar(bool show);

int chatPromptMode();        // 0=精简, 1=完整
void setChatPromptMode(int mode);

} // namespace AppSettings

#endif // APPSETTINGS_H
