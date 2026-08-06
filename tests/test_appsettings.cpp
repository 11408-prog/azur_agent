#include <gtest/gtest.h>
#include <QSettings>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonArray>
#include "data/appsettings.h"

// ---------------------------------------------------------------------
// 重要：测试隔离
//
// AppSettings 内部固定用 QSettings("AzurStudio", "AzurAgent")，默认走
// NativeFormat —— Windows 上是注册表 HKCU\Software\AzurStudio\AzurAgent，
// Linux 上是 ~/.config/AzurStudio/AzurAgent.conf。这跟真实安装的 app 是
// 同一份存储，之前的测试会读到开发机上已有的真实配置（比如你手动开过
// "自动执行"，DefaultPermissionIsZero 就会失败），而且测试本身还会
// 反过来污染你的真实配置。
//
// 这里用一个全局 GTest Environment，在所有测试跑之前把 QSettings 的默认
// 格式/路径重定向到一个临时目录下的 ini 文件。因为 appsettings.cpp 里的
// `static QSettings s("AzurStudio","AzurAgent")` 是函数局部静态变量，
// 只要在第一次调用任何 AppSettings::xxx() 之前完成重定向，之后所有读写
// 都会落在这个隔离的临时文件里，不会碰真实配置，也不依赖运行测试的机器
// 上已经装了什么。
// ---------------------------------------------------------------------

namespace {
class IsolatedSettingsEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        tempDir = new QTemporaryDir();
        ASSERT_TRUE(tempDir->isValid()) << "无法创建隔离测试用的临时目录";
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir->path());
    }

    void TearDown() override {
        delete tempDir;
    }

private:
    QTemporaryDir *tempDir = nullptr;
};
} // namespace

// 注册全局 Environment。GTest 会在所有测试之前调用一次 SetUp()。
static ::testing::Environment *const g_isolatedSettingsEnv =
    ::testing::AddGlobalTestEnvironment(new IsolatedSettingsEnvironment());

// ---------------------------------------------------------------------
// 默认值
// ---------------------------------------------------------------------

TEST(AppSettingsTest, DefaultModelIsNotEmpty) {
    QString model = AppSettings::model();
    EXPECT_FALSE(model.isEmpty());
}

TEST(AppSettingsTest, DefaultPermissionIsZero) {
    int perm = AppSettings::agentPermission();
    EXPECT_EQ(perm, 0);
}

TEST(AppSettingsTest, StartupModeDefaultIsZero) {
    int mode = AppSettings::startupMode();
    EXPECT_EQ(mode, 0);
}

// ---------------------------------------------------------------------
// 读写往返（round-trip）——之前完全没有覆盖过"写进去、读出来"这条路径
// ---------------------------------------------------------------------

TEST(AppSettingsTest, ChatModel_SetThenGet_RoundTrips) {
    AppSettings::setChatModel("test-model-123");
    EXPECT_EQ(AppSettings::chatModel(), "test-model-123");
}

TEST(AppSettingsTest, ProjectModel_SetThenGet_RoundTrips) {
    AppSettings::setProjectModel("project-model-abc");
    EXPECT_EQ(AppSettings::projectModel(), "project-model-abc");
}

TEST(AppSettingsTest, AgentPermission_SetThenGet_RoundTrips) {
    AppSettings::setAgentPermission(1);
    EXPECT_EQ(AppSettings::agentPermission(), 1);
    AppSettings::setAgentPermission(0);
    EXPECT_EQ(AppSettings::agentPermission(), 0);
}

TEST(AppSettingsTest, ChatBgEnabled_BooleanRoundTrips) {
    AppSettings::setChatBgEnabled(true);
    EXPECT_TRUE(AppSettings::chatBgEnabled());
    AppSettings::setChatBgEnabled(false);
    EXPECT_FALSE(AppSettings::chatBgEnabled());
}

TEST(AppSettingsTest, RecentModels_ListRoundTrips) {
    const QStringList models = {"model-a", "model-b", "model-c"};
    AppSettings::setRecentModels(models);
    EXPECT_EQ(AppSettings::recentModels(), models);
}

TEST(AppSettingsTest, ProjectHistory_JsonArrayRoundTrips) {
    QJsonArray history;
    QJsonObject entry;
    entry["path"] = "/some/project";
    entry["name"] = "SomeProject";
    history.append(entry);

    AppSettings::setProjectHistory(history);
    QJsonArray readBack = AppSettings::projectHistory();

    ASSERT_EQ(readBack.size(), 1);
    EXPECT_EQ(readBack[0].toObject()["path"].toString(), "/some/project");
}

TEST(AppSettingsTest, BgOpacity_IntRoundTrips) {
    AppSettings::setBgOpacity(42);
    EXPECT_EQ(AppSettings::bgOpacity(), 42);
}

TEST(AppSettingsTest, LastProjectPath_StringRoundTrips) {
    AppSettings::setLastProjectPath("/tmp/my-project");
    EXPECT_EQ(AppSettings::lastProjectPath(), "/tmp/my-project");
}
