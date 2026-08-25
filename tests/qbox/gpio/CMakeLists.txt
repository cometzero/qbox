add_executable(qemu-pl061-test qemu-pl061-test.cc)
target_include_directories(qemu-pl061-test PRIVATE ${QBOX_TEST_DIR}/include)
target_link_libraries(qemu-pl061-test PRIVATE qemu_pl061 ${TARGET_LIBS})
add_test(
    NAME qemu-pl061-test
    COMMAND $<TARGET_FILE:qemu-pl061-test> -p test-bench.gpio.init_inputs=90
)
set_tests_properties(qemu-pl061-test PROPERTIES TIMEOUT 30)
