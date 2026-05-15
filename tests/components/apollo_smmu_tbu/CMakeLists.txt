macro(gs_add_test test)
    add_executable(${test} ${test}.cc)
    target_link_libraries(${test} PRIVATE gtest gmock apollo_smmu_tbu ${TARGET_LIBS})
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endmacro()

gs_add_test(apollo-smmu-tbu-tests)
