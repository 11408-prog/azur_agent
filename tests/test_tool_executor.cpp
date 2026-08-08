#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonObject>
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
        // s_allowedPaths 是类级别的静态状态，测试之间必须清空，
        // 否则前一个测试设置的白名单会泄漏到下一个测试里。
        ToolExecutor::setAllowedPaths({});
        delete tempDir;
    }

    QTemporaryDir* tempDir;
    QString workspaceRoot;
};

// ---------------------------------------------------------------------
// resolveSafePath
// ---------------------------------------------------------------------

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

TEST_F(ToolExecutorTest, ResolveSafePath_EmptyPath_ResolvesToWorkspaceRoot) {
    // 实现里空字符串会被当作 "." 处理，落回工作区根目录本身，而不是报错。
    bool ok = false;
    QString result = ToolExecutor::resolveSafePath(workspaceRoot, "", &ok);
    EXPECT_TRUE(ok);
    EXPECT_EQ(QDir::cleanPath(result), QDir::cleanPath(QDir(workspaceRoot).canonicalPath()));
}

TEST_F(ToolExecutorTest, ResolveSafePath_WhitelistedOutsidePath_Accepted) {
    QTemporaryDir otherDir;
    ASSERT_TRUE(otherDir.isValid());
    QFile f(otherDir.path() + "/extra.txt");
    f.open(QIODevice::WriteOnly);
    f.write("x");
    f.close();

    ToolExecutor::setAllowedPaths({otherDir.path()});
    bool ok = false;
    QString result = ToolExecutor::resolveSafePath(workspaceRoot, otherDir.path() + "/extra.txt", &ok);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(result.isEmpty());
}

TEST_F(ToolExecutorTest, ResolveSafePath_NonWhitelistedOutsidePath_Rejected) {
    QTemporaryDir otherDir;
    ASSERT_TRUE(otherDir.isValid());
    // 没有调用 setAllowedPaths，白名单为空，应该被拒绝
    bool ok = true;
    ToolExecutor::resolveSafePath(workspaceRoot, otherDir.path(), &ok);
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------
// isWriteTool
//
// 当前工具集里只有 read_file / list_directory 两个只读工具，
// 没有任何写操作工具了，isWriteTool() 恒返回 false。
// ---------------------------------------------------------------------

TEST(ToolExecutorWriteToolTest, AlwaysReturnsFalse) {
    EXPECT_FALSE(ToolExecutor::isWriteTool("read_file"));
    EXPECT_FALSE(ToolExecutor::isWriteTool("list_directory"));
    // 即便传入历史上曾经存在过的写操作工具名，也应该返回 false
    // （这些工具本身已经不在 toolDefinitions()/execute() 里注册了）。
    EXPECT_FALSE(ToolExecutor::isWriteTool("write_file"));
    EXPECT_FALSE(ToolExecutor::isWriteTool("run_command"));
}

// ---------------------------------------------------------------------
// read_file / list_directory 基础健全性
// ---------------------------------------------------------------------

TEST_F(ToolExecutorTest, ReadFile_ExistingFile_ReturnsContent) {
    QJsonObject args;
    args["path"] = "src/main.cpp";
    bool ok = false;
    QString label;
    QString result = ToolExecutor::execute(workspaceRoot, "read_file", args, &ok, &label);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains("int main()"));
}

TEST_F(ToolExecutorTest, ReadFile_NonExistentFile_ReturnsError) {
    QJsonObject args;
    args["path"] = "src/nope.cpp";
    bool ok = true;
    QString label;
    ToolExecutor::execute(workspaceRoot, "read_file", args, &ok, &label);
    EXPECT_FALSE(ok);
}

TEST_F(ToolExecutorTest, ReadFile_PathOutsideWorkspace_Rejected) {
    QJsonObject args;
#ifdef Q_OS_UNIX
    args["path"] = "../../etc/passwd";
#else
    args["path"] = "C:/Windows/System32/drivers/etc/hosts";
#endif
    bool ok = true;
    QString label;
    QString result = ToolExecutor::execute(workspaceRoot, "read_file", args, &ok, &label);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("拒绝访问")));
}

TEST_F(ToolExecutorTest, ListDirectory_ReturnsEntries) {
    QJsonObject args;
    args["path"] = ".";
    bool ok = false;
    QString label;
    QString result = ToolExecutor::execute(workspaceRoot, "list_directory", args, &ok, &label);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains("src"));
}

TEST_F(ToolExecutorTest, ListDirectory_NonExistentDir_ReturnsError) {
    QJsonObject args;
    args["path"] = "does_not_exist";
    bool ok = true;
    QString label;
    ToolExecutor::execute(workspaceRoot, "list_directory", args, &ok, &label);
    EXPECT_FALSE(ok);
}

TEST_F(ToolExecutorTest, Execute_UnknownTool_ReturnsError) {
    bool ok = true;
    QString label;
    QString result = ToolExecutor::execute(workspaceRoot, "not_a_real_tool", {}, &ok, &label);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("不支持的工具")));
}

TEST_F(ToolExecutorTest, Execute_InvalidWorkspaceRoot_ReturnsError) {
    bool ok = true;
    QString label;
    QString result = ToolExecutor::execute("/path/that/does/not/exist", "read_file",
                                            {{"path", "x"}}, &ok, &label);
    EXPECT_FALSE(ok);
}

TEST_F(ToolExecutorTest, ToolDefinitions_OnlyExposesReadOnlyTools) {
    const QJsonArray tools = ToolExecutor::toolDefinitions();
    EXPECT_EQ(tools.size(), 2);

    QStringList names;
    for (const QJsonValue &v : tools) {
        names << v.toObject()["function"].toObject()["name"].toString();
    }
    EXPECT_TRUE(names.contains("read_file"));
    EXPECT_TRUE(names.contains("list_directory"));
}
