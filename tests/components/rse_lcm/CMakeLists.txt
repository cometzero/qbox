add_executable(rse_lcm-tests rse_lcm-tests.cc)
target_link_libraries(rse_lcm-tests PRIVATE gtest gmock rse_lcm ${TARGET_LIBS})
add_test(NAME rse_lcm-tests COMMAND rse_lcm-tests)
set_tests_properties(rse_lcm-tests PROPERTIES TIMEOUT 20)
