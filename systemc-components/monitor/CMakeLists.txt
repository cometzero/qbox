add_library(runtime_action_service INTERFACE)
target_include_directories(runtime_action_service INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:systemc-components/monitor/include>
)
install(TARGETS runtime_action_service EXPORT qqvpTargets)

gs_create_dymod(monitor)
target_link_libraries(monitor PUBLIC runtime_action_service)

file(GLOB_RECURSE monitor_static_files CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/static/*")
set_property(TARGET monitor APPEND PROPERTY LINK_DEPENDS
    ${monitor_static_files})

add_custom_command(TARGET monitor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/static $<TARGET_FILE_DIR:monitor>/static)

install (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/static DESTINATION ${CMAKE_BINARY_DIR})
