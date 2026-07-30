#include "data/appsettings.h"

#include <QSettings>

namespace {

// 唯一一处出现 "AzurStudio"/"AzurAgent" 字符串常量的地方。
QSettings &store()
{
    static QSettings s("AzurStudio", "AzurAgent");
    return s;
}

} // namespace

namespace AppSettings {

QString apiKey() { return store().value("apiKey").toString().trimmed(); }
void setApiKey(const QString &key) { store().setValue("apiKey", key); }

QString baseUrl() { return store().value("baseUrl", "https://api.deepseek.com").toString().trimmed(); }
void setBaseUrl(const QString &url) { store().setValue("baseUrl", url); }

QString model() { return store().value("model", "deepseek-v4-flash").toString().trimmed(); }
void setModel(const QString &m) { store().setValue("model", m); }

QStringList recentModels() { return store().value("recentModels").toStringList(); }
void setRecentModels(const QStringList &models) { store().setValue("recentModels", models); }

int agentPermission() { return store().value("agentPermission", 0).toInt(); }
void setAgentPermission(int mode) { store().setValue("agentPermission", mode); }

int startupMode() { return store().value("startupMode", 0).toInt(); }
void setStartupMode(int mode) { store().setValue("startupMode", mode); }

int bgOpacity() { return store().value("bgOpacity", 25).toInt(); }
void setBgOpacity(int opacity) { store().setValue("bgOpacity", opacity); }

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

} // namespace AppSettings
