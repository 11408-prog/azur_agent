# tests/CMakeLists.txt - unit test module

set(TEST_SOURCES
    ../core/tool_executor.cpp
    ../core/contextpolicy.cpp
    ../data/appsettings.cpp
    ../core/git_executor.cpp
    # add other sources if needed, e.g. ../core/git_executor.cpp
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
    Qt6::Network          # git_executor 可能用到 QNetworkAccessManager 等
    gtest_main
    Ela::Widgets          # 如果 tool_executor 或 git_executor 引用了 Ela 符号
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
