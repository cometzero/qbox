add_executable(rse_sysctrl-tests rse_sysctrl-tests.cc)
target_link_libraries(rse_sysctrl-tests PRIVATE gtest gmock rse_sysctrl ${TARGET_LIBS})
add_test(NAME rse_sysctrl-tests COMMAND rse_sysctrl-tests)
set_tests_properties(rse_sysctrl-tests PROPERTIES TIMEOUT 20)
