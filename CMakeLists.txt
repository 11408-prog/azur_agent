cmake_minimum_required(VERSION 3.19)
project(agent_ LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)
qt_standard_project_setup()

# ============ 1. 使用本地预编译的 ElaWidgetTools ============
add_library(Ela::Widgets SHARED IMPORTED)
set_target_properties(Ela::Widgets PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/lib/ElaWidgetTools/include"
    IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/ElaWidgetTools/lib/libElaWidgetTools.dll.a"
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/lib/ElaWidgetTools/bin/ElaWidgetTools.dll"
)

# ============ 2. 创建可执行目标 ============
qt_add_executable(agent_
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    chatpagewidget.cpp
    chatpagewidget.h
    settingpagewidget.cpp
    settingpagewidget.h
    ai_client.cpp
    ai_client.h
    conversationmanager.cpp
    conversationmanager.h
    tool_executor.cpp
    tool_executor.h
    projectsession.cpp
    projectsession.h
    agent_engine.cpp
    agent_engine.h
    projectpage.cpp
    projectpage.h
    activitypanel.cpp
    activitypanel.h
    markdownrenderer.cpp
    markdownrenderer.h
    promptloader.cpp
    promptloader.h
    messagebubblewidget.cpp
    messagebubblewidget.h
    project_analyzer.cpp
    project_analyzer.h
    projecthistorydialog.cpp
    projecthistorydialog.h
    projectconvdialog.cpp
    projectconvdialog.h
    app.qrc
)

# ============ 3. 链接库 ============
target_compile_definitions(agent_ PRIVATE
    RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources"
)

target_link_libraries(agent_
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Ela::Widgets
)

# ============ 4. 安装规则 ============
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
