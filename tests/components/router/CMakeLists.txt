macro(gs_add_test test)
    get_filename_component(qbox_source_dir
        "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    add_executable(${test} ${test}.cc)
    target_include_directories(${test} PRIVATE
        ${qbox_source_dir}/tests/qbox/include
        ${qbox_source_dir}/systemc-components/common/include
        ${qbox_source_dir}/systemc-components/common/include/tests
    )
    target_link_libraries(${test} PRIVATE gtest gmock router pass ${TARGET_LIBS})
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endmacro()
gs_add_test(router-tests)
gs_add_test(router-tests-extended)
gs_add_test(router-addressmap-tests)
gs_add_test(router-cache-bench-enhanced)
gs_add_test(router-advanced-overlap-tests)
gs_add_test(router-shadowing-warning-test)
gs_add_test(router-tests-new)
gs_add_test(router-thread-safety-test)
gs_add_test(router-coverage-tests)

set_tests_properties(router-cache-bench-enhanced PROPERTIES TIMEOUT 60 SKIP_TEST TRUE)
