#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "core/tool_executor.h"

class ToolExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir = new QTemporaryDir();
        ASSERT_TRUE(tempDir->isValid()) << "Cannot create temp dir";
        workspaceRoot = tempDir->path();

        QDir dir(workspaceRoot);
        dir.mkdir("src");
        dir.mkdir("sub");
        QFile f(workspaceRoot + "/src/main.cpp");
        f.open(QIODevice::WriteOnly);
        f.write("int main() { return 0; }");
        f.close();
    }

    void TearDown() override {
        delete tempDir;
    }

    QTemporaryDir* tempDir;
    QString workspaceRoot;
};

TEST_F(ToolExecutorTest, ResolveSafePath_ValidPath_ReturnsAbsolute) {
    bool ok = false;
    QString result = ToolExecutor::resolveSafePath(workspaceRoot, "src/main.cpp", &ok);
    EXPECT_TRUE(ok);
    EXPECT_EQ(result, QDir(workspaceRoot).absoluteFilePath("src/main.cpp"));
}

TEST_F(ToolExecutorTest, ResolveSafePath_PathTraversal_Rejected) {
    bool ok = true;
    QString result = ToolExecutor::resolveSafePath(workspaceRoot, "../../etc/passwd", &ok);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(ToolExecutorTest, ResolveSafePath_RelativeWithinWorkspace_Accepted) {
    bool ok = false;
    QString result = ToolExecutor::resolveSafePath(workspaceRoot, "./sub/../src/main.cpp", &ok);
    EXPECT_TRUE(ok);
    EXPECT_EQ(result, QDir(workspaceRoot).absoluteFilePath("src/main.cpp"));
}

TEST_F(ToolExecutorTest, ResolveSafePath_AbsoluteOutside_Rejected) {
#ifdef Q_OS_UNIX
    QString outside = "/etc/passwd";
#else
    QString outside = "C:/Windows/System32/drivers/etc/hosts";
#endif
    bool ok = true;
    ToolExecutor::resolveSafePath(workspaceRoot, outside, &ok);
    EXPECT_FALSE(ok);
}

TEST(ToolExecutorBlacklistTest, BlacklistBlocksDangerousRm) {
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("rm -rf /", &reason));
    EXPECT_TRUE(reason.contains("閫掑綊/寮哄埗鍒犻櫎"));
}

TEST(ToolExecutorBlacklistTest, BlacklistBlocksRmWithRecursiveFlag) {
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("rm -r some_dir", &reason));
    EXPECT_TRUE(reason.contains("閫掑綊/寮哄埗鍒犻櫎"));
}

TEST(ToolExecutorBlacklistTest, BlacklistAllowsSafeRm) {
    QString reason;
    EXPECT_FALSE(ToolExecutor::isBlacklistedCommand("rm file.txt", &reason));
    EXPECT_TRUE(reason.isEmpty());
}

TEST(ToolExecutorBlacklistTest, BlacklistBlocksSudo) {
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("sudo apt update", &reason));
    EXPECT_TRUE(reason.contains("绂佹鎵ц"));
}

TEST(ToolExecutorBlacklistTest, BlacklistBlocksChmod) {
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("chmod 777 /etc", &reason));
    EXPECT_TRUE(reason.contains("绂佹鎵ц"));
}
