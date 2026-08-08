# core
target_sources(agent_ PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_client.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agent_engine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agent_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tool_executor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tool_executor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/promptloader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/promptloader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/contextpolicy.h
    ${CMAKE_CURRENT_SOURCE_DIR}/contextpolicy.cpp
)
