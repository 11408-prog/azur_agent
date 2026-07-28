#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "tool_executor.h"

// ToolExecutor::resolveSafePath 本身是 private 静态方法，这里统一通过公开的
// execute() 接口（read_file / write_file）间接验证路径安全校验的行为，
// 这样测试用例也顺带覆盖了 execute() 的分发逻辑。
class TstToolExecutor : public QObject
{
    Q_OBJECT

private slots:
    void init();    // 每个测试用例前都重新准备一个干净的临时工作区
    void cleanup();

    // ---- 路径安全 / 防穿越 ----
    void readFile_insideWorkspace_succeeds();
    void readFile_parentTraversal_rejected();
    void readFile_absolutePathOutside_rejected();
    void readFile_nonexistentFile_reportsError();
    void readFile_emptyPath_reportsError();

    void writeFile_newFile_succeedsAndProducesDiff();
    void writeFile_traversal_rejected();
    void writeFile_existingFile_producesUnifiedDiff();

    void allowedPaths_whitelist_permitsOutsideAccess();
    void allowedPaths_notInWhitelist_stillRejected();

    // ---- 命令黑名单 ----
    void blacklist_rejectsDangerousCommands_data();
    void blacklist_rejectsDangerousCommands();
    void blacklist_allowsSafeCommands_data();
    void blacklist_allowsSafeCommands();

private:
    QTemporaryDir *workspace_ = nullptr;
    QTemporaryDir *outsideDir_ = nullptr;
    QString workspacePath_;
};

void TstToolExecutor::init()
{
    workspace_ = new QTemporaryDir();
    outsideDir_ = new QTemporaryDir();
    QVERIFY(workspace_->isValid());
    QVERIFY(outsideDir_->isValid());
    workspacePath_ = workspace_->path();

    // 工作区内放一个源文件，方便测试正常读取
    QDir(workspacePath_).mkpath("src");
    QFile f(workspacePath_ + "/src/main.cpp");
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream(&f) << "int main() { return 0; }\n";
    f.close();

    // 工作区外放一个"秘密文件"，模拟越权访问的目标
    QFile secret(outsideDir_->path() + "/secret.txt");
    QVERIFY(secret.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream(&secret) << "top secret\n";
    secret.close();

    // 确保每个用例开始前白名单是空的，避免用例之间互相影响
    ToolExecutor::setAllowedPaths(QStringList());
}

void TstToolExecutor::cleanup()
{
    ToolExecutor::setAllowedPaths(QStringList());
    delete workspace_;
    delete outsideDir_;
    workspace_ = nullptr;
    outsideDir_ = nullptr;
}

void TstToolExecutor::readFile_insideWorkspace_succeeds()
{
    bool ok = false;
    QString label;
    const QString content = ToolExecutor::execute(workspacePath_, "read_file",
                                                    QJsonObject{ {"path", "src/main.cpp"} },
                                                    &ok, &label);
    QVERIFY(ok);
    QVERIFY(content.contains("int main()"));
}

void TstToolExecutor::readFile_parentTraversal_rejected()
{
    bool ok = true; // 故意先设为 true，确认函数会把它改成 false
    QString label;
    const QString result = ToolExecutor::execute(workspacePath_, "read_file",
                                                   QJsonObject{ {"path", "../secret.txt"} },
                                                   &ok, &label);
    QVERIFY(!ok);
    QVERIFY2(result.contains(QStringLiteral("拒绝访问")),
             qPrintable("实际返回信息: " + result));
}

void TstToolExecutor::readFile_absolutePathOutside_rejected()
{
    bool ok = true;
    QString label;
    const QString absOutside = outsideDir_->path() + "/secret.txt";
    const QString result = ToolExecutor::execute(workspacePath_, "read_file",
                                                   QJsonObject{ {"path", absOutside} },
                                                   &ok, &label);
    QVERIFY(!ok);
    QVERIFY2(result.contains(QStringLiteral("拒绝访问")),
             qPrintable("实际返回信息: " + result));
}

void TstToolExecutor::readFile_nonexistentFile_reportsError()
{
    bool ok = true;
    QString label;
    const QString result = ToolExecutor::execute(workspacePath_, "read_file",
                                                   QJsonObject{ {"path", "src/does_not_exist.cpp"} },
                                                   &ok, &label);
    QVERIFY(!ok);
    QVERIFY(result.contains(QStringLiteral("不存在")));
}

void TstToolExecutor::readFile_emptyPath_reportsError()
{
    bool ok = true;
    QString label;
    const QString result = ToolExecutor::execute(workspacePath_, "read_file",
                                                   QJsonObject{ {"path", ""} },
                                                   &ok, &label);
    QVERIFY(!ok);
    QVERIFY(result.contains(QStringLiteral("未提供")));
}

void TstToolExecutor::writeFile_newFile_succeedsAndProducesDiff()
{
    bool ok = false;
    QString label;
    QString diff;
    const QString result = ToolExecutor::execute(workspacePath_, "write_file",
                                                   QJsonObject{ {"path", "src/new_file.txt"},
                                                                {"content", "hello\nworld\n"} },
                                                   &ok, &label, &diff);
    QVERIFY(ok);
    Q_UNUSED(result);
    QVERIFY(QFile::exists(workspacePath_ + "/src/new_file.txt"));
    QVERIFY2(diff.contains(QStringLiteral("创建新文件")), qPrintable("实际 diff: " + diff));
    QVERIFY(diff.contains("+ hello"));
}

void TstToolExecutor::writeFile_traversal_rejected()
{
    bool ok = true;
    QString label;
    const QString result = ToolExecutor::execute(workspacePath_, "write_file",
                                                   QJsonObject{ {"path", "../evil.txt"},
                                                                {"content", "pwned"} },
                                                   &ok, &label);
    QVERIFY(!ok);
    QVERIFY(result.contains(QStringLiteral("拒绝访问")));
    // 确认工作区外真的没有被创建文件
    QVERIFY(!QFile::exists(outsideDir_->path() + "/evil.txt"));
}

void TstToolExecutor::writeFile_existingFile_producesUnifiedDiff()
{
    bool ok = false;
    QString label;
    QString diff;
    const QString result = ToolExecutor::execute(workspacePath_, "write_file",
                                                   QJsonObject{ {"path", "src/main.cpp"},
                                                                {"content", "int main() { return 1; }\n"} },
                                                   &ok, &label, &diff);
    QVERIFY(ok);
    Q_UNUSED(result);
    // 已存在文件的写入应该走 generateDiff 分支，而不是"创建新文件"分支
    QVERIFY(!diff.contains(QStringLiteral("创建新文件")));
}

void TstToolExecutor::allowedPaths_whitelist_permitsOutsideAccess()
{
    ToolExecutor::setAllowedPaths(QStringList{ outsideDir_->path() });

    bool ok = false;
    QString label;
    const QString content = ToolExecutor::execute(workspacePath_, "read_file",
                                                    QJsonObject{ {"path", outsideDir_->path() + "/secret.txt"} },
                                                    &ok, &label);
    QVERIFY(ok);
    QVERIFY(content.contains("top secret"));
}

void TstToolExecutor::allowedPaths_notInWhitelist_stillRejected()
{
    // 白名单里有一个完全不相关的目录，不应该意外放行 outsideDir_
    QTemporaryDir unrelated;
    QVERIFY(unrelated.isValid());
    ToolExecutor::setAllowedPaths(QStringList{ unrelated.path() });

    bool ok = true;
    QString label;
    const QString result = ToolExecutor::execute(workspacePath_, "read_file",
                                                   QJsonObject{ {"path", outsideDir_->path() + "/secret.txt"} },
                                                   &ok, &label);
    QVERIFY(!ok);
    QVERIFY(result.contains(QStringLiteral("拒绝访问")));
}

void TstToolExecutor::blacklist_rejectsDangerousCommands_data()
{
    QTest::addColumn<QString>("command");
    QTest::newRow("rm -rf") << "rm -rf /tmp/whatever";
    QTest::newRow("rm -fr") << "rm -fr build";
    QTest::newRow("rm --recursive") << "rm --recursive build";
    QTest::newRow("format") << "format c:";
    QTest::newRow("mkfs") << "mkfs.ext4 /dev/sda1";
    QTest::newRow("dd") << "dd if=/dev/zero of=/dev/sda";
    QTest::newRow("shutdown") << "shutdown -h now";
    QTest::newRow("sudo") << "sudo apt install foo";
    QTest::newRow("del (Windows)") << "del /f /q C:\\Windows";
    QTest::newRow("chmod") << "chmod 777 /etc/passwd";
}

void TstToolExecutor::blacklist_rejectsDangerousCommands()
{
    QFETCH(QString, command);
    QString reason;
    QVERIFY2(ToolExecutor::isBlacklistedCommand(command, &reason),
             qPrintable("期望被拦截: " + command));
    QVERIFY(!reason.isEmpty());
}

void TstToolExecutor::blacklist_allowsSafeCommands_data()
{
    QTest::addColumn<QString>("command");
    QTest::newRow("rm single file") << "rm build/output.o";
    QTest::newRow("ls") << "ls -la";
    QTest::newRow("git status") << "git status";
    QTest::newRow("npm install (local)") << "npm install lodash";

    // ↓↓↓ 已知的黑名单局限：只检查命令的第一个词，脚本语言可以在内部
    // 调用文件删除 API 完全绕过黑名单。这条用例不是在断言"这样做是安全的"，
    // 而是记录当前的真实行为，防止以后有人误以为黑名单已经堵上了这个口子。
    // 如果之后要加固 isBlacklistedCommand，这条用例应该被改成 QVERIFY(拦截)。
    QTest::newRow("KNOWN GAP: python rmtree bypass")
        << "python3 -c \"import shutil; shutil.rmtree('/tmp/x')\"";
}

void TstToolExecutor::blacklist_allowsSafeCommands()
{
    QFETCH(QString, command);
    QString reason;
    QVERIFY2(!ToolExecutor::isBlacklistedCommand(command, &reason),
             qPrintable("不应被拦截: " + command + "，但返回原因: " + reason));
}

QTEST_APPLESS_MAIN(TstToolExecutor)
#include "tst_tool_executor.moc"
