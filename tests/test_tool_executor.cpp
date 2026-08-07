#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include "core/tool_executor.h"

namespace {
QJsonObject makePatch(const QString &search, const QString &replace) {
    QJsonObject p;
    p["search"] = search;
    p["replace"] = replace;
    return p;
}
} // namespace

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
// isBlacklistedCommand
//
// 之前这里 4 个断言里的中文期望字符串是乱码（GBK/UTF-8 编码错位导致），
// 跟 tool_executor.cpp 里实际产生的 "禁止递归/强制删除文件" / "被禁止执行（高危操作）"
// 完全不是同一批字符，.contains() 永远返回 false，这些测试之前是必然失败的。
// ---------------------------------------------------------------------

TEST(ToolExecutorBlacklistTest, BlacklistBlocksDangerousRm) {
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("rm -rf /", &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("递归/强制删除")));
}

TEST(ToolExecutorBlacklistTest, BlacklistBlocksRmWithRecursiveFlag) {
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("rm -r some_dir", &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("递归/强制删除")));
}

TEST(ToolExecutorBlacklistTest, BlacklistBlocksCombinedShortFlags) {
    // 覆盖 README 里特别提到的场景：-irf / -Rf / -fri 这类合并短选项簇
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("rm -irf some_dir", &reason));
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("rm -Rf some_dir", &reason));
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("rm -fri some_dir", &reason));
}

TEST(ToolExecutorBlacklistTest, BlacklistAllowsSafeRm) {
    QString reason;
    EXPECT_FALSE(ToolExecutor::isBlacklistedCommand("rm file.txt", &reason));
    EXPECT_TRUE(reason.isEmpty());
}

TEST(ToolExecutorBlacklistTest, BlacklistAllowsInteractiveRmFlag) {
    // "-i"（交互确认）不含 r/f，不应该被拦
    QString reason;
    EXPECT_FALSE(ToolExecutor::isBlacklistedCommand("rm -i file.txt", &reason));
}

TEST(ToolExecutorBlacklistTest, BlacklistBlocksSudo) {
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("sudo apt update", &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("被禁止执行")));
}

TEST(ToolExecutorBlacklistTest, BlacklistBlocksChmod) {
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("chmod 777 /etc", &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("被禁止执行")));
}

TEST(ToolExecutorBlacklistTest, BlacklistBlocksDiskDestructiveCommands) {
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("dd if=/dev/zero of=/dev/sda", &reason));
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("mkfs /dev/sda1", &reason)); // 改成 mkfs
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("shutdown -h now", &reason));
}

TEST(ToolExecutorBlacklistTest, BlacklistBlocksPipeToSudo) {
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("echo x | sudo tee /etc/hosts", &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("管道提权")));
}

TEST(ToolExecutorBlacklistTest, BlacklistIsCaseInsensitive) {
    // isBlacklistedCommand 内部先 toLower()，大写命令也应该被拦
    QString reason;
    EXPECT_TRUE(ToolExecutor::isBlacklistedCommand("SUDO apt update", &reason));
}

TEST(ToolExecutorBlacklistTest, BlacklistAllowsOrdinaryCommand) {
    QString reason;
    EXPECT_FALSE(ToolExecutor::isBlacklistedCommand("git status", &reason));
    EXPECT_FALSE(ToolExecutor::isBlacklistedCommand("npm install", &reason));
}

// ---------------------------------------------------------------------
// isWriteTool
// ---------------------------------------------------------------------

TEST(ToolExecutorWriteToolTest, WriteToolsIdentifiedCorrectly) {
    EXPECT_TRUE(ToolExecutor::isWriteTool("write_file"));
    EXPECT_TRUE(ToolExecutor::isWriteTool("apply_patch"));
    EXPECT_TRUE(ToolExecutor::isWriteTool("run_command"));
    EXPECT_TRUE(ToolExecutor::isWriteTool("git_commit"));
}

TEST(ToolExecutorWriteToolTest, ReadToolsAreNotWriteTools) {
    EXPECT_FALSE(ToolExecutor::isWriteTool("read_file"));
    EXPECT_FALSE(ToolExecutor::isWriteTool("list_directory"));
    EXPECT_FALSE(ToolExecutor::isWriteTool("git_status"));
    EXPECT_FALSE(ToolExecutor::isWriteTool("git_log"));
}

// ---------------------------------------------------------------------
// write_file（通过公开的 execute() 入口，覆盖 writeFile 的实际行为）
// ---------------------------------------------------------------------

TEST_F(ToolExecutorTest, WriteFile_NewFile_CreatesAndReportsSuccess) {
    QJsonObject args;
    args["path"] = "src/new.txt";
    args["content"] = "hello\nworld";

    bool ok = false;
    QString label, diff;
    QString result = ToolExecutor::execute(workspaceRoot, "write_file", args, &ok, &label, &diff);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(QFile::exists(workspaceRoot + "/src/new.txt"));
    EXPECT_TRUE(diff.contains(QStringLiteral("创建新文件")));

    QFile f(workspaceRoot + "/src/new.txt");
    f.open(QIODevice::ReadOnly);
    EXPECT_EQ(QString::fromUtf8(f.readAll()), "hello\nworld");
}

TEST_F(ToolExecutorTest, WriteFile_OverwriteExisting_ProducesDiff) {
    QJsonObject args;
    args["path"] = "src/main.cpp";
    args["content"] = "int main() { return 1; }";

    bool ok = false;
    QString label, diff;
    ToolExecutor::execute(workspaceRoot, "write_file", args, &ok, &label, &diff);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(diff.contains(QStringLiteral("创建新文件")));
    // diff 应该同时体现旧内容被删、新内容被加
    EXPECT_TRUE(diff.contains("-") && diff.contains("+"));
}

TEST_F(ToolExecutorTest, WriteFile_PathTraversal_Rejected) {
    QJsonObject args;
    args["path"] = "../outside.txt";
    args["content"] = "x";

    bool ok = true;
    QString label, diff;
    QString result = ToolExecutor::execute(workspaceRoot, "write_file", args, &ok, &label, &diff);

    EXPECT_FALSE(ok);
    EXPECT_FALSE(QFile::exists(QDir(workspaceRoot).absoluteFilePath("../outside.txt")));
}

TEST_F(ToolExecutorTest, WriteFile_MissingPath_ReturnsError) {
    QJsonObject args;
    args["content"] = "x";

    bool ok = true;
    QString label, diff;
    QString result = ToolExecutor::execute(workspaceRoot, "write_file", args, &ok, &label, &diff);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("错误")));
}

TEST_F(ToolExecutorTest, WriteFile_OversizedContent_Rejected) {
    QJsonObject args;
    args["path"] = "src/big.txt";
    // 超过 1MB 上限
    args["content"] = QString(1024 * 1024 + 10, QChar('a'));

    bool ok = true;
    QString label, diff;
    QString result = ToolExecutor::execute(workspaceRoot, "write_file", args, &ok, &label, &diff);

    EXPECT_FALSE(ok);
    EXPECT_FALSE(QFile::exists(workspaceRoot + "/src/big.txt"));
}

// ---------------------------------------------------------------------
// apply_patch（README 里明确点名过的高风险区域：精确匹配 / 模糊匹配 / 歧义检测）
// ---------------------------------------------------------------------

TEST_F(ToolExecutorTest, ApplyPatch_ExactMatch_Succeeds) {
    QJsonObject args;
    args["path"] = "src/main.cpp";
    QJsonArray patches;
    patches.append(makePatch("return 0;", "return 1;"));
    args["patches"] = patches;

    bool ok = false;
    QString label, diff;
    QString result = ToolExecutor::execute(workspaceRoot, "apply_patch", args, &ok, &label, &diff);

    EXPECT_TRUE(ok);
    QFile f(workspaceRoot + "/src/main.cpp");
    f.open(QIODevice::ReadOnly);
    EXPECT_TRUE(QString::fromUtf8(f.readAll()).contains("return 1;"));
}

TEST_F(ToolExecutorTest, ApplyPatch_MultiplePatches_AppliedInOrder) {
    QFile f(workspaceRoot + "/src/multi.txt");
    f.open(QIODevice::WriteOnly);
    f.write("line1\nline2\nline3\n");
    f.close();

    QJsonObject args;
    args["path"] = "src/multi.txt";
    QJsonArray patches;
    patches.append(makePatch("line1", "LINE1"));
    patches.append(makePatch("line3", "LINE3"));
    args["patches"] = patches;

    bool ok = false;
    QString label, diff;
    ToolExecutor::execute(workspaceRoot, "apply_patch", args, &ok, &label, &diff);

    EXPECT_TRUE(ok);
    QFile out(workspaceRoot + "/src/multi.txt");
    out.open(QIODevice::ReadOnly);
    QString content = QString::fromUtf8(out.readAll());
    EXPECT_TRUE(content.contains("LINE1"));
    EXPECT_TRUE(content.contains("LINE3"));
    EXPECT_TRUE(content.contains("line2")); // 未涉及的行保持原样
}

TEST_F(ToolExecutorTest, ApplyPatch_FuzzyWhitespaceMatch_Succeeds) {
    QFile f(workspaceRoot + "/src/fuzzy.txt");
    f.open(QIODevice::WriteOnly);
    f.write("    indented line\n");
    f.close();

    QJsonObject args;
    args["path"] = "src/fuzzy.txt";
    QJsonArray patches;
    // search 里首尾空白跟文件里的不一致，应该走模糊匹配 fallback 成功
    patches.append(makePatch("indented line", "changed line"));
    args["patches"] = patches;

    bool ok = false;
    QString label, diff;
    ToolExecutor::execute(workspaceRoot, "apply_patch", args, &ok, &label, &diff);

    EXPECT_TRUE(ok);
}

TEST_F(ToolExecutorTest, ApplyPatch_SearchNotFound_FailsWithoutModifyingFile) {
    QJsonObject args;
    args["path"] = "src/main.cpp";
    QJsonArray patches;
    patches.append(makePatch("this text does not exist anywhere", "x"));
    args["patches"] = patches;

    bool ok = true;
    QString label, diff;
    QString result = ToolExecutor::execute(workspaceRoot, "apply_patch", args, &ok, &label, &diff);

    EXPECT_FALSE(ok);
    QFile f(workspaceRoot + "/src/main.cpp");
    f.open(QIODevice::ReadOnly);
    EXPECT_EQ(QString::fromUtf8(f.readAll()), "int main() { return 0; }"); // 原文件未被改动
}

TEST_F(ToolExecutorTest, ApplyPatch_AmbiguousMatch_FailsAndReportsMultipleMatches) {
    QFile f(workspaceRoot + "/src/dup.txt");
    f.open(QIODevice::WriteOnly);
    f.write("foo\nfoo\n");
    f.close();

    QJsonObject args;
    args["path"] = "src/dup.txt";
    QJsonArray patches;
    patches.append(makePatch("foo", "bar"));
    args["patches"] = patches;

    bool ok = true;
    QString label, diff;
    QString result = ToolExecutor::execute(workspaceRoot, "apply_patch", args, &ok, &label, &diff);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("多处匹配")));
}

TEST_F(ToolExecutorTest, ApplyPatch_PartialFailureDoesNotPartiallyApply) {
    // 第一条 patch 能匹配，第二条不能：整体应该失败，且第一条也不能生效
    QJsonObject args;
    args["path"] = "src/main.cpp";
    QJsonArray patches;
    patches.append(makePatch("int main()", "int MAIN()"));
    patches.append(makePatch("does not exist", "x"));
    args["patches"] = patches;

    bool ok = true;
    QString label, diff;
    ToolExecutor::execute(workspaceRoot, "apply_patch", args, &ok, &label, &diff);

    EXPECT_FALSE(ok);
    QFile f(workspaceRoot + "/src/main.cpp");
    f.open(QIODevice::ReadOnly);
    EXPECT_EQ(QString::fromUtf8(f.readAll()), "int main() { return 0; }");
}

TEST_F(ToolExecutorTest, ApplyPatch_EmptyPatchesArray_ReturnsError) {
    QJsonObject args;
    args["path"] = "src/main.cpp";
    args["patches"] = QJsonArray();

    bool ok = true;
    QString label, diff;
    QString result = ToolExecutor::execute(workspaceRoot, "apply_patch", args, &ok, &label, &diff);
    EXPECT_FALSE(ok);
}

TEST_F(ToolExecutorTest, ApplyPatch_TooManyPatches_Rejected) {
    QJsonObject args;
    args["path"] = "src/main.cpp";
    QJsonArray patches;
    for (int i = 0; i < 25; ++i) { // 超过上限 20
        patches.append(makePatch(QString("x%1").arg(i), "y"));
    }
    args["patches"] = patches;

    bool ok = true;
    QString label, diff;
    QString result = ToolExecutor::execute(workspaceRoot, "apply_patch", args, &ok, &label, &diff);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("超过上限")));
}

TEST_F(ToolExecutorTest, ApplyPatch_FileNotExist_ReturnsError) {
    QJsonObject args;
    args["path"] = "src/does_not_exist.cpp";
    QJsonArray patches;
    patches.append(makePatch("a", "b"));
    args["patches"] = patches;

    bool ok = true;
    QString label, diff;
    QString result = ToolExecutor::execute(workspaceRoot, "apply_patch", args, &ok, &label, &diff);
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------
// previewDiff 与 execute() 结果一致性
//
// 这是回归测试的重点：README 明确写过历史上 previewDiff 和真正执行是两份
// 独立实现，容易跑偏（"预览看到的"和"实际发生的"不一致）。现在统一成
// applyPatchesToContent() 一份逻辑，这里验证 previewDiff() 判定的成功/失败
// 结果要跟 execute() 真正执行后的结果一致。
// ---------------------------------------------------------------------

TEST_F(ToolExecutorTest, PreviewDiff_ApplyPatch_MatchesActualExecution_Success) {
    QJsonObject args;
    args["path"] = "src/main.cpp";
    QJsonArray patches;
    patches.append(makePatch("return 0;", "return 42;"));
    args["patches"] = patches;

    bool previewOk = false;
    QString previewLabel, previewDiffText;
    QString previewResult = ToolExecutor::previewDiff(workspaceRoot, "apply_patch", args, &previewOk, &previewLabel);
    previewDiffText = previewResult;

    bool execOk = false;
    QString execLabel, execDiff;
    ToolExecutor::execute(workspaceRoot, "apply_patch", args, &execOk, &execLabel, &execDiff);

    EXPECT_TRUE(previewOk);
    EXPECT_TRUE(execOk);
    EXPECT_TRUE(previewDiffText.contains("return 42;"));
}

TEST_F(ToolExecutorTest, PreviewDiff_ApplyPatch_MatchesActualExecution_Failure) {
    // 预览判定为失败（歧义匹配）时，真正执行也必须同样失败，
    // 不能出现"预览说不行，执行却生效了"的情况。
    QFile f(workspaceRoot + "/src/dup2.txt");
    f.open(QIODevice::WriteOnly);
    f.write("dup\ndup\n");
    f.close();

    QJsonObject args;
    args["path"] = "src/dup2.txt";
    QJsonArray patches;
    patches.append(makePatch("dup", "changed"));
    args["patches"] = patches;

    bool previewOk = true;
    QString previewLabel;
    ToolExecutor::previewDiff(workspaceRoot, "apply_patch", args, &previewOk, &previewLabel);
    EXPECT_FALSE(previewOk);

    bool execOk = true;
    QString execLabel, execDiff;
    ToolExecutor::execute(workspaceRoot, "apply_patch", args, &execOk, &execLabel, &execDiff);
    EXPECT_FALSE(execOk);

    QFile check(workspaceRoot + "/src/dup2.txt");
    check.open(QIODevice::ReadOnly);
    EXPECT_EQ(QString::fromUtf8(check.readAll()), "dup\ndup\n"); // 文件未被改动
}

TEST_F(ToolExecutorTest, PreviewDiff_WriteFile_NewFile_ShowsCreationMarker) {
    QJsonObject args;
    args["path"] = "src/brand_new.txt";
    args["content"] = "content";

    bool ok = false;
    QString label;
    QString result = ToolExecutor::previewDiff(workspaceRoot, "write_file", args, &ok, &label);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("创建新文件")));
    // previewDiff 不应该真的创建文件
    EXPECT_FALSE(QFile::exists(workspaceRoot + "/src/brand_new.txt"));
}

// ---------------------------------------------------------------------
// read_file / list_directory 基础健全性（之前完全没覆盖）
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

TEST_F(ToolExecutorTest, ListDirectory_ReturnsEntries) {
    QJsonObject args;
    args["path"] = ".";
    bool ok = false;
    QString label;
    QString result = ToolExecutor::execute(workspaceRoot, "list_directory", args, &ok, &label);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains("src"));
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
