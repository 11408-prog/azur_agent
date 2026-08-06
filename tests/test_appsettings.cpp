#include <gtest/gtest.h>
#include "data/appsettings.h"

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
