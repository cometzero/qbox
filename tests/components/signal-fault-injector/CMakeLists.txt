add_executable(signal_fault_injector-tests signal-fault-injector-tests.cc)
target_link_libraries(signal_fault_injector-tests PRIVATE signal_fault_injector gtest gmock ${TARGET_LIBS})
add_test(NAME signal_fault_injector-tests COMMAND signal_fault_injector-tests)
set_tests_properties(signal_fault_injector-tests PROPERTIES TIMEOUT 20 LABELS "runtime-injection")
