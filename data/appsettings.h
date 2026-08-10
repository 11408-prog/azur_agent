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
int themeMode();            // 0=浅色, 1=深色, 2=跟随系统
void setThemeMode(int mode);

int bgOpacity();
void setBgOpacity(int opacity);

bool chatBgEnabled();
void setChatBgEnabled(bool enabled);

bool showStatusBar();
void setShowStatusBar(bool show);

int chatPromptMode();        // 0=精简, 1=完整
void setChatPromptMode(int mode);

// ---- 语音朗读 ----
bool ttsEnabled();
void setTtsEnabled(bool enabled);

QString ttsVoice();
void setTtsVoice(const QString &voice);

// ---- 事实记忆（P3） ----
// 开启后每次回复结束会用 LLM 抽取「值得长期记住的事实」，并在后续请求注入。
// 默认关闭：不开启则完全不产生额外的 LLM 调用，行为与过去完全一致。
bool memoryEnabled();
void setMemoryEnabled(bool enabled);

// ---- 工具调用 ----
// 工作区根目录：read_file / list_directory 只能访问这个目录内的路径。
// 为空表示未启用工具调用（MainWindow 传空 tools 数组给引擎）。
QString workspaceRoot();
void setWorkspaceRoot(const QString &path);

} // namespace AppSettings

#endif // APPSETTINGS_H
