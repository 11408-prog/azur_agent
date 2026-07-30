cmake_minimum_required(VERSION 3.19)
project(agent_ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive-)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)
qt_standard_project_setup()

# ============ ElaWidgetTools 预编译库 ============
add_library(Ela::Widgets SHARED IMPORTED)
set_target_properties(Ela::Widgets PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/lib/ElaWidgetTools/include"
    IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/ElaWidgetTools/lib/libElaWidgetTools.dll.a"
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/lib/ElaWidgetTools/bin/ElaWidgetTools.dll"
)

# ============ 创建可执行目标 ============
qt_add_executable(agent_ WIN32 MACOSX_BUNDLE)

target_compile_definitions(agent_ PRIVATE
    RESOURCES_DIR="${CMAKE_SOURCE_DIR}/app/resources"
)

# 以项目根为 include 基路径，所有 #include "module/file.h" 可正确解析
target_include_directories(agent_ PRIVATE ${CMAKE_SOURCE_DIR})

# ============ 各模块 ============
add_subdirectory(core)
add_subdirectory(data)
add_subdirectory(ui)
add_subdirectory(chat)
add_subdirectory(project)
add_subdirectory(app)

# ============ 链接 ============
target_link_libraries(agent_
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Ela::Widgets
)

# ============ 安装规则 ============
install(TARGETS agent_
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET agent_
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
