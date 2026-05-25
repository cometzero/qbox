add_executable(strata_flash_j3-tests strata_flash_j3-tests.cc)
target_link_libraries(strata_flash_j3-tests PRIVATE gtest gmock strata_flash_j3 ${TARGET_LIBS})
add_test(NAME strata_flash_j3-tests COMMAND strata_flash_j3-tests)
set_tests_properties(strata_flash_j3-tests PROPERTIES TIMEOUT 20)
