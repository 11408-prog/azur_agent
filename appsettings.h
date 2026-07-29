#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QString>
#include <QStringList>
#include <QJsonArray>

// 统一封装项目里所有 QSettings("AzurStudio", "AzurAgent") 的读写。
//
// 之前 "AzurStudio"/"AzurAgent" 这两个字符串常量、以及一堆裸的 key 名字符串
// （"apiKey" / "agentPermission" / "projectHistory" / ...）分散写在
// mainwindow.cpp / modemanager.cpp / projectconversationservice.cpp /
// projecthistorydialog.cpp / projectpage.cpp / settingpagewidget.cpp
// 六个文件、21+ 处地方，每处都要手打一遍，容易打错，也没法一眼看出
// "这个项目到底存了哪些配置项、默认值是什么"。
// 现在全部集中到这里，改 key 名字/默认值只需要改一处。
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

// ---- Agent 行为 ----
// 0 = 每次确认写操作/命令（默认），1 = 自动执行（跳过确认弹窗）
int agentPermission();
void setAgentPermission(int mode);

// 0 = 启动后进入聊天模式（默认），1 = 启动后进入项目模式
int startupMode();
void setStartupMode(int mode);

// ---- UI ----
int bgOpacity();
void setBgOpacity(int opacity);

bool projectLeftPanelCollapsed();
void setProjectLeftPanelCollapsed(bool collapsed);

bool projectRightPanelCollapsed();
void setProjectRightPanelCollapsed(bool collapsed);

// ---- 项目相关 ----
QString lastProjectPath();
void setLastProjectPath(const QString &path);

QJsonArray projectHistory();
void setProjectHistory(const QJsonArray &history);

bool projectConvMigrationDone();
void setProjectConvMigrationDone(bool done);

} // namespace AppSettings

#endif // APPSETTINGS_H
