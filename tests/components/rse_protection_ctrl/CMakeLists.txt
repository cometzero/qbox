add_executable(rse_protection_ctrl-tests rse_protection_ctrl-tests.cc)
target_link_libraries(rse_protection_ctrl-tests PRIVATE gtest gmock rse_protection_ctrl ${TARGET_LIBS})
add_test(NAME rse_protection_ctrl-tests COMMAND rse_protection_ctrl-tests)
set_tests_properties(rse_protection_ctrl-tests PROPERTIES TIMEOUT 20)
