add_executable(monitor-runtime-api-tests monitor-runtime-api-tests.cc)
target_include_directories(monitor-runtime-api-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../qbox/include)
target_link_libraries(monitor-runtime-api-tests PRIVATE monitor ${TARGET_LIBS})

add_executable(monitor-runtime-config-tests monitor-runtime-config-tests.cc)
target_link_libraries(monitor-runtime-config-tests PRIVATE monitor gtest gmock ${TARGET_LIBS})
add_test(NAME monitor-runtime-config-tests COMMAND monitor-runtime-config-tests)
set_tests_properties(monitor-runtime-config-tests PROPERTIES TIMEOUT 20 LABELS "runtime-injection")

foreach(scenario disabled missing full)
    if(scenario STREQUAL "disabled")
        set(mutation false)
        set(service test-bench.service)
    elseif(scenario STREQUAL "missing")
        set(mutation true)
        set(service test-bench.missing)
    else()
        set(mutation true)
        set(service test-bench.service)
    endif()
    add_test(
        NAME monitor-runtime-api-${scenario}
        COMMAND $<TARGET_FILE:monitor-runtime-api-tests>
            -p test-bench.test_mode=${scenario}
            -p test-bench.monitor.server_port=0
            -p test-bench.monitor.runtime_mutation=${mutation}
            -p test-bench.monitor.injection_service=${service}
            -p test-bench.monitor.use_html_presentation=false
    )
    set_tests_properties(monitor-runtime-api-${scenario} PROPERTIES TIMEOUT 20 LABELS "runtime-injection")
endforeach()
