add_executable(arm_system_counter-tests arm-system-counter-tests.cc)
target_link_libraries(arm_system_counter-tests PRIVATE
    arm_system_counter
    gtest
    gmock
    ${TARGET_LIBS}
)
add_test(NAME arm_system_counter-tests COMMAND arm_system_counter-tests)
set_tests_properties(arm_system_counter-tests PROPERTIES TIMEOUT 30)
