#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QJsonObject>
#include "core/git_executor.h"

// ---------------------------------------------------------------------
// git_executor.cpp 之前虽然已经被加进了 tests/CMakeLists.txt 的
// TEST_SOURCES（编译进了测试目标），但一个测试用例都没有写。
//
// 这里的测试依赖系统上真的装了 `git` 命令（跟被测代码本身的实现方式一致，
// GitExecutor 就是直接 QProcess 调 git）。如果测试机没装 git，用
// GTEST_SKIP() 优雅跳过而不是报错刷屏。
// ---------------------------------------------------------------------

namespace {
bool gitAvailable() {
    QProcess p;
    p.start("git", {"--version"});
    return p.waitForStarted(2000) && p.waitForFinished(2000) && p.exitCode() == 0;
}

// 运行一条 git 命令用于测试环境搭建（不是被测代码路径）
void runGitSetup(const QString &workDir, const QStringList &args) {
    QProcess p;
    p.setWorkingDirectory(workDir);
    p.start("git", args);
    ASSERT_TRUE(p.waitForFinished(5000)) << "git setup command timed out: " << args.join(' ').toStdString();
}
} // namespace

class GitExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!gitAvailable()) {
            GTEST_SKIP() << "系统未安装 git，跳过 GitExecutor 测试";
        }
        tempDir = new QTemporaryDir();
        ASSERT_TRUE(tempDir->isValid());
        repoRoot = tempDir->path();

        runGitSetup(repoRoot, {"init", "-q"});
        // 用仓库局部配置，不依赖/不污染开发机的全局 git 配置
        runGitSetup(repoRoot, {"config", "user.email", "test@example.com"});
        runGitSetup(repoRoot, {"config", "user.name", "Test Runner"});
        // 部分新版 git 默认分支名不统一，显式固定，避免测试对分支名断言不稳定
        runGitSetup(repoRoot, {"symbolic-ref", "HEAD", "refs/heads/main"});
    }

    void TearDown() override {
        delete tempDir;
    }

    void writeFile(const QString &relPath, const QString &content) {
        QFile f(repoRoot + "/" + relPath);
        f.open(QIODevice::WriteOnly);
        f.write(content.toUtf8());
        f.close();
    }

    QTemporaryDir *tempDir = nullptr;
    QString repoRoot;
};

TEST_F(GitExecutorTest, Status_UntrackedFile_ReportsIt) {
    writeFile("new.txt", "content");

    bool ok = false;
    QString label;
    QString result = GitExecutor::status(repoRoot, {}, &ok, &label);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains("new.txt"));
    EXPECT_TRUE(result.contains(QStringLiteral("未跟踪")));
}

TEST_F(GitExecutorTest, Status_CleanRepo_ReportsClean) {
    // 空仓库、没有任何变更
    bool ok = false;
    QString label;
    QString result = GitExecutor::status(repoRoot, {}, &ok, &label);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("工作区干净")));
}

TEST_F(GitExecutorTest, Status_InvalidRepo_ReturnsError) {
    QTemporaryDir notARepo;
    ASSERT_TRUE(notARepo.isValid());

    bool ok = true;
    QString label;
    GitExecutor::status(notARepo.path(), {}, &ok, &label);
    EXPECT_FALSE(ok);
}

TEST_F(GitExecutorTest, Commit_EmptyMessage_RejectedWithoutTouchingGit) {
    writeFile("f.txt", "x");
    QJsonObject args;
    args["message"] = "";

    bool ok = true;
    QString label;
    QString result = GitExecutor::commit(repoRoot, args, &ok, &label);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("不能为空")));
}

TEST_F(GitExecutorTest, Commit_StagesAndCommitsAllChanges) {
    writeFile("f.txt", "hello");

    QJsonObject args;
    args["message"] = "initial commit";

    bool ok = false;
    QString label;
    QString result = GitExecutor::commit(repoRoot, args, &ok, &label);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("提交成功")));

    // 提交之后 status 应该是干净的
    bool statusOk = false;
    QString statusLabel;
    QString statusResult = GitExecutor::status(repoRoot, {}, &statusOk, &statusLabel);
    EXPECT_TRUE(statusResult.contains(QStringLiteral("工作区干净")));
}

TEST_F(GitExecutorTest, Commit_NothingToCommit_StillReportsOk) {
    writeFile("f.txt", "hello");
    QJsonObject args;
    args["message"] = "first";
    bool ok = false;
    QString label;
    GitExecutor::commit(repoRoot, args, &ok, &label);
    ASSERT_TRUE(ok);

    // 再提交一次，这次没有任何变更
    QJsonObject args2;
    args2["message"] = "second, nothing changed";
    bool ok2 = false;
    QString label2;
    QString result2 = GitExecutor::commit(repoRoot, args2, &ok2, &label2);
    EXPECT_TRUE(ok2);
    EXPECT_TRUE(result2.contains(QStringLiteral("没有需要提交")));
}

TEST_F(GitExecutorTest, Log_AfterCommit_ShowsCommitMessage) {
    writeFile("f.txt", "hello");
    QJsonObject commitArgs;
    commitArgs["message"] = "log test commit";
    bool commitOk = false;
    QString commitLabel;
    GitExecutor::commit(repoRoot, commitArgs, &commitOk, &commitLabel);
    ASSERT_TRUE(commitOk);

    bool ok = false;
    QString label;
    QString result = GitExecutor::log(repoRoot, {}, &ok, &label);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains("log test commit"));
}

TEST_F(GitExecutorTest, Log_EmptyRepo_ReportsNoCommits) {
    bool ok = false;
    QString label;
    QString result = GitExecutor::log(repoRoot, {}, &ok, &label);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("没有提交记录")));
}

TEST_F(GitExecutorTest, Log_RespectsMaxCount) {
    for (int i = 0; i < 5; ++i) {
        writeFile("f.txt", QString("content %1").arg(i));
        QJsonObject args;
        args["message"] = QString("commit %1").arg(i);
        bool ok = false;
        QString label;
        GitExecutor::commit(repoRoot, args, &ok, &label);
        ASSERT_TRUE(ok);
    }

    QJsonObject logArgs;
    logArgs["max_count"] = 2;
    bool ok = false;
    QString label;
    QString result = GitExecutor::log(repoRoot, logArgs, &ok, &label);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains("commit 4")); // 最新的应该在
    EXPECT_FALSE(result.contains("commit 0")); // 太旧的应该被截掉
}

TEST_F(GitExecutorTest, Diff_AfterModification_ShowsChange) {
    writeFile("f.txt", "line1\n");
    QJsonObject commitArgs;
    commitArgs["message"] = "base";
    bool commitOk = false;
    QString commitLabel;
    GitExecutor::commit(repoRoot, commitArgs, &commitOk, &commitLabel);
    ASSERT_TRUE(commitOk);

    writeFile("f.txt", "line1\nline2\n");

    bool ok = false;
    QString label;
    QString result = GitExecutor::diff(repoRoot, {}, &ok, &label);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains("line2"));
}

TEST_F(GitExecutorTest, Diff_NoChanges_ReportsNoChanges) {
    writeFile("f.txt", "line1\n");
    QJsonObject commitArgs;
    commitArgs["message"] = "base";
    bool commitOk = false;
    QString commitLabel;
    GitExecutor::commit(repoRoot, commitArgs, &commitOk, &commitLabel);
    ASSERT_TRUE(commitOk);

    bool ok = false;
    QString label;
    QString result = GitExecutor::diff(repoRoot, {}, &ok, &label);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("无变更")));
}

TEST_F(GitExecutorTest, Branch_ListsCurrentBranch) {
    writeFile("f.txt", "x");
    QJsonObject commitArgs;
    commitArgs["message"] = "init";
    bool commitOk = false;
    QString commitLabel;
    GitExecutor::commit(repoRoot, commitArgs, &commitOk, &commitLabel);
    ASSERT_TRUE(commitOk);

    bool ok = false;
    QString label;
    QString result = GitExecutor::branch(repoRoot, {}, &ok, &label);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains("main"));
}
