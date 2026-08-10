# tests/CMakeLists.txt - unit test module

# CMAKE_AUTOMOC 必须在 add_executable() 之前打开，测试目标里的
# ai_client.cpp 是个 QObject 派生类（DeepSeekClient，带信号槽），
# 需要 moc 生成配套代码才能编译链接。之前只测纯函数/命名空间的
# 三个模块用不到这个，现在加了 ai_client 才需要打开。
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(TEST_SOURCES
    ../core/tool_executor.cpp
    ../core/contextpolicy.cpp
    ../data/appsettings.cpp
    ../core/promptloader.cpp
    ../core/ai_client.cpp
    ../core/agent_engine.cpp
    ../core/ttsclient.cpp
    ../core/memoryclient.cpp
)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

add_executable(azur_agent_tests
    test_main.cpp
    test_tool_executor.cpp
    test_context_policy.cpp
    test_appsettings.cpp
    test_promptloader.cpp
    test_ai_client.cpp
    test_agent_engine.cpp
    test_python_backend.cpp
    test_tts_client.cpp
    test_memory_client.cpp
    ${TEST_SOURCES}
    # app.qrc 打包进测试可执行文件，是为了测试 PromptLoader::loadFile()
    # 在 RESOURCES_DIR 源码树路径不可用时，回退读取 qrc 内容这条路径
    # （历史上这条路径因为别名前缀不匹配静默失效过，属于要重点盯防回归的地方）
    ${CMAKE_SOURCE_DIR}/app/app.qrc
)

# PromptLoader::loadFile() 优先读这个编译时写死的源码树路径，和主程序
# （见根 CMakeLists.txt）保持一致，这样"从真实文件读取"这条路径也能测到。
target_compile_definitions(azur_agent_tests PRIVATE
    RESOURCES_DIR="${CMAKE_SOURCE_DIR}/app/resources"
    # test_python_backend.cpp 用项目根定位 azur_agent 虚拟环境与 python/azur_tools/cli.py
    PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

find_package(Qt6 COMPONENTS Core Network Test REQUIRED)
target_link_libraries(azur_agent_tests
    PRIVATE
    Qt6::Core
    Qt6::Network          # ai_client.cpp 用到 QNetworkAccessManager/QNetworkReply
    Qt6::Test              # test_ai_client.cpp 用 QSignalSpy 观察信号
    gtest                  # main 由 test_main.cpp 提供（需先建 QCoreApplication）
    # 注：测试覆盖的源文件完全不依赖 ElaWidgetTools，测试目标不链接
    # Ela::Widgets，这样跑单元测试不需要先编译 ElaWidgetTools 库（这一步
    # 在只想构建主程序时也是可选的，见根 CMakeLists.txt 的 AZUR_BUILD_TESTS
    # 开关）。如果以后测试覆盖到了引用 Ela 符号的模块，再把 Ela::Widgets 加回来。
)

# 为测试目标启用预编译头（加速 Qt + GTest 编译）
target_precompile_headers(azur_agent_tests PRIVATE
    <QtCore/QtCore>
    <gtest/gtest.h>
)

target_include_directories(azur_agent_tests PRIVATE
    ..
    ../core
    ../data
)

include(GoogleTest)

add_test(NAME azur_agent_tests COMMAND azur_agent_tests)
