// 测试主入口。
//
// 用自定义 main 替代 gtest_main：必须先创建 QCoreApplication 实例，
// 否则 ToolExecutor::executeViaPython() 里的 QProcess 无法正常启动
// Python 子进程（会静默失败导致回退原生实现、测试误判）。
#include <QCoreApplication>
#include <gtest/gtest.h>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
