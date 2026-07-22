add_executable(qemu-arm-generic-timer-bridge-tests
    qemu-arm-generic-timer-bridge-tests.cc)
target_link_libraries(qemu-arm-generic-timer-bridge-tests PRIVATE
    arm_system_counter
    qemu_arm_generic_timer_counter_bridge
    qemu_arm_arch_timer_mmio
    qemu_sse_timer
    gtest
    gmock
    ${TARGET_LIBS}
)
add_test(NAME qemu-arm-generic-timer-bridge-tests
    COMMAND qemu-arm-generic-timer-bridge-tests)
set_tests_properties(qemu-arm-generic-timer-bridge-tests PROPERTIES TIMEOUT 60)

add_executable(libqemu-v2-negotiation-tests
    libqemu-v2-negotiation-tests.cc)
target_link_libraries(libqemu-v2-negotiation-tests PRIVATE
    gtest
    gmock
    ${TARGET_LIBS}
)
add_test(NAME libqemu-v2-negotiation-tests
    COMMAND libqemu-v2-negotiation-tests)
