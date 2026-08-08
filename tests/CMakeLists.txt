# tests/CMakeLists.txt - unit test module

set(TEST_SOURCES
    ../core/tool_executor.cpp
    ../core/contextpolicy.cpp
    ../data/appsettings.cpp
)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

add_executable(azur_agent_tests
    test_tool_executor.cpp
    test_context_policy.cpp
    test_appsettings.cpp
    ${TEST_SOURCES}
)

find_package(Qt6 COMPONENTS Core REQUIRED)
target_link_libraries(azur_agent_tests
    PRIVATE
    Qt6::Core
    gtest_main
    # 注：test_tool_executor / test_context_policy / test_appsettings 覆盖的三个
    # 源文件（tool_executor.cpp / contextpolicy.cpp / appsettings.cpp）完全不依赖
    # ElaWidgetTools，测试目标不链接 Ela::Widgets，这样跑单元测试不需要先编译
    # ElaWidgetTools 库（这一步在只想构建主程序时也是可选的，见根 CMakeLists.txt
    # 的 AZUR_BUILD_TESTS 开关）。如果以后测试覆盖到了引用 Ela 符号的模块，
    # 再把 Ela::Widgets 加回来。
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