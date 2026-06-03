add_executable(host_system_pll-tests host_system_pll-tests.cc)
target_link_libraries(host_system_pll-tests PRIVATE gtest gmock host_system_pll ${TARGET_LIBS})
add_test(NAME host_system_pll-tests COMMAND host_system_pll-tests)
set_tests_properties(host_system_pll-tests PROPERTIES TIMEOUT 20)
