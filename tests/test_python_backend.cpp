#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QDebug>
#include "core/tool_executor.h"

// 验证 ToolExecutor 的 Python 后端（QProcess 调 azur_agent 里的 cli.py）。
//
// 前置条件：
//   1. 项目根下存在虚拟环境 azur_agent/（含 Scripts/python.exe）
//   2. 项目根下存在 python/azur_tools/cli.py
//   3. CMake 配置时传入了 -DPROJECT_ROOT_DIR=项目根（见 tests/CMakeLists.txt）
//
// 如果 venv 或 cli.py 不存在，相关用例会 SKIP，不会误报失败。

namespace {

// 通过编译期传入的项目根，定位 venv 与 cli.py，避免依赖 QCoreApplication 实例。
QString venvInterpreter()
{
    return QStringLiteral(PROJECT_ROOT_DIR) + "/azur_agent/Scripts/python.exe";
}

QString toolCli()
{
    return QStringLiteral(PROJECT_ROOT_DIR) + "/python/azur_tools/cli.py";
}

} // namespace

class PythonBackendTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir = new QTemporaryDir();
        ASSERT_TRUE(tempDir->isValid()) << "Cannot create temp dir";
        workspaceRoot = tempDir->path();

        QDir dir(workspaceRoot);
        dir.mkdir("src");
        QFile f(workspaceRoot + "/src/main.cpp");
        ASSERT_TRUE(f.open(QIODevice::WriteOnly)) << "Cannot write temp source file";
        f.write("int main() { return 0; } // hello python backend");
        f.close();

        // 显式指定解释器与 cli.py 路径，让测试不依赖 findUpward 自动查找
        ToolExecutor::setPythonInterpreterPath(venvInterpreter());
        ToolExecutor::setPythonToolCliPath(toolCli());

        ToolExecutor::setAllowedPaths({});
        // 每个用例自行决定是否开启后端，TearDown 统一复位
    }

    void TearDown() override {
        ToolExecutor::setUsePythonBackend(false);
        ToolExecutor::setAllowedPaths({});
        ToolExecutor::setPythonInterpreterPath(QString());
        ToolExecutor::setPythonToolCliPath(QString());
        delete tempDir;
    }

    bool backendPrereqMet() const {
        return QFile::exists(venvInterpreter()) && QFile::exists(toolCli());
    }

    QTemporaryDir* tempDir;
    QString workspaceRoot;
};

// ---------------------------------------------------------------------
// read_file 走 Python 后端
// ---------------------------------------------------------------------

TEST_F(PythonBackendTest, ReadFile_ViaPythonBackend) {
    if (!backendPrereqMet()) GTEST_SKIP() << "未找到 azur_agent 虚拟环境或 cli.py，跳过";

    ToolExecutor::setUsePythonBackend(true);
    EXPECT_TRUE(ToolExecutor::usePythonBackend());

    QJsonObject args;
    args["path"] = "src/main.cpp";
    bool ok = false;
    QString label;
    QString result = ToolExecutor::execute(workspaceRoot, "read_file", args, &ok, &label);
    EXPECT_TRUE(ok) << "Python 后端 read_file 失败: " << result.toStdString();
    EXPECT_TRUE(result.contains("hello python backend"));
    // 后端正常工作的情况下，开关应该保持开启（不会被关闭）
    EXPECT_TRUE(ToolExecutor::usePythonBackend());
}

TEST_F(PythonBackendTest, ReadFile_ViaPythonBackend_PathTraversalRejected) {
    if (!backendPrereqMet()) GTEST_SKIP() << "未找到 azur_agent 虚拟环境或 cli.py，跳过";

    ToolExecutor::setUsePythonBackend(true);

    QJsonObject args;
    args["path"] = "../../etc/passwd";
    bool ok = true;
    QString label;
    QString result = ToolExecutor::execute(workspaceRoot, "read_file", args, &ok, &label);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(result.contains(QStringLiteral("不在工作区目录内")));
}

// ---------------------------------------------------------------------
// list_directory 走 Python 后端
// ---------------------------------------------------------------------

TEST_F(PythonBackendTest, ListDirectory_ViaPythonBackend) {
    if (!backendPrereqMet()) GTEST_SKIP() << "未找到 azur_agent 虚拟环境或 cli.py，跳过";

    ToolExecutor::setUsePythonBackend(true);

    QJsonObject args;
    args["path"] = ".";
    bool ok = false;
    QString label;
    QString result = ToolExecutor::execute(workspaceRoot, "list_directory", args, &ok, &label);
    EXPECT_TRUE(ok) << "Python 后端 list_directory 失败: " << result.toStdString();
    EXPECT_TRUE(result.contains("[目录] src/"));
}

// ---------------------------------------------------------------------
// 后端不可用时自动回退原生实现并关闭开关
// ---------------------------------------------------------------------

TEST_F(PythonBackendTest, FallsBackToNativeWhenBackendMissing) {
    // 故意给一个不存在的解释器路径，模拟后端基础设施不可用
    ToolExecutor::setPythonInterpreterPath("C:/nonexistent/python.exe");
    ToolExecutor::setUsePythonBackend(true);

    QJsonObject args;
    args["path"] = "src/main.cpp";
    bool ok = false;
    QString label;
    QString result = ToolExecutor::execute(workspaceRoot, "read_file", args, &ok, &label);
    // 回退到原生实现，仍然成功
    EXPECT_TRUE(ok);
    EXPECT_TRUE(result.contains("hello python backend"));
    // 开关应被关闭，避免后续每次调用都白试一次
    EXPECT_FALSE(ToolExecutor::usePythonBackend());
}
