qbox_add_cpu_test(aarch64-simple-write-test 100 simple-write-test.cc)
qbox_add_cpu_test_executable(
    aarch64-single-tcg-five-cpu-shutdown-test
    single-tcg-five-cpu-shutdown-test.cc)
if(TARGET aarch64-single-tcg-five-cpu-shutdown-test)
    add_test(
        NAME aarch64-single-tcg-five-cpu-shutdown-test
        COMMAND $<TARGET_FILE:aarch64-single-tcg-five-cpu-shutdown-test>
            -p test-bench.inst.accel=tcg
            -p test-bench.inst.tcg_mode=SINGLE
            -p test-bench.inst.sync_policy=multithread-quantum
            -p test-bench.inst.time_sync_strategy=quantum_keeper
            -p log_level=0
    )
    set_tests_properties(
        aarch64-single-tcg-five-cpu-shutdown-test
        PROPERTIES TIMEOUT 30)
endif()
qbox_add_cpu_test(aarch64-dmi-test 100 dmi-test.cc)
qbox_add_cpu_test(aarch64-dmi-test-concurrent-inval 100 dmi-test-concurrent-inval.cc)
# Build assembly firmware for DMI reset test
find_program(LLVM_MC llvm-mc HINTS /opt/homebrew/opt/llvm/bin)
find_program(LLD ld.lld)
find_program(LLVM_OBJCOPY llvm-objcopy HINTS /opt/homebrew/opt/llvm/bin)

# "and APPLE" workaround: QQVPQSP-714
if(LLVM_MC AND LLD AND LLVM_OBJCOPY AND APPLE)
    # Assemble the ARM64 assembly file
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.o
        COMMAND ${LLVM_MC} -arch=aarch64 -filetype=obj -triple=aarch64-none-elf
                ${CMAKE_CURRENT_SOURCE_DIR}/dmi-test-reset.S
                -o ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.o
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dmi-test-reset.S
        COMMENT "Assembling dmi-test-reset.S"
    )

    # Link the object file
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.elf
        COMMAND ${LLD} -T ${CMAKE_CURRENT_SOURCE_DIR}/dmi-test-reset.ld
                ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.o
                -o ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.elf
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.o
                ${CMAKE_CURRENT_SOURCE_DIR}/dmi-test-reset.ld
        COMMENT "Linking dmi-test-reset.elf"
    )

    # Create binary file
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.bin
        COMMAND ${LLVM_OBJCOPY} -O binary
                ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.elf
                ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.bin
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.elf
        COMMENT "Creating dmi-test-reset.bin"
    )

    # Create a custom target for the firmware
    add_custom_target(dmi-test-reset-firmware
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.bin)

    qbox_add_cpu_test(aarch64-dmi-test-reset 100 dmi-test-reset.cc)
    add_dependencies(aarch64-dmi-test-reset dmi-test-reset-firmware)
    target_compile_definitions(aarch64-dmi-test-reset PRIVATE
        FIRMWARE_BIN_PATH="${CMAKE_CURRENT_BINARY_DIR}/dmi-test-reset.bin")
else()
    message(STATUS
        "Skipping aarch64-dmi-test-reset; it requires macOS and LLVM tools")
endif()
qbox_add_cpu_test(aarch64-ld-st-excl-fail-test 100 ld-st-excl-fail.cc)
qbox_add_cpu_test(aarch64-write_read 100 write_read.cc)
qbox_add_cpu_test(aarch64-dmi-test-async-inval 100 dmi-test-async-inval.cc)
qbox_add_cpu_test(halt-tests 100 halt-tests.cc)
qbox_add_cpu_test(reset-test-system 100 reset-test-system.cc)
qbox_add_cpu_test(reset-test-cpu 100 reset-test-cpu.cc)
qbox_add_cpu_test_executable(aarch64-start-in-reset-release-test start-in-reset-release-test.cc)
if(TARGET aarch64-start-in-reset-release-test)
    add_test(
        NAME aarch64-start-in-reset-release-test
        COMMAND $<TARGET_FILE:aarch64-start-in-reset-release-test>
            -p test-bench.inst.managed_start_in_reset_release=true
            -p test-bench.inst.accel=tcg
            -p test-bench.inst.tcg_mode=MULTI
            -p test-bench.inst.sync_policy=multithread-quantum
            -p test-bench.inst.time_sync_strategy=quantum_keeper
            -p log_level=0
    )
    set_tests_properties(aarch64-start-in-reset-release-test PROPERTIES TIMEOUT 30)
endif()
qbox_add_cpu_test_executable(aarch64-managed-timer-wfi-test managed-timer-wfi-test.cc)
if(TARGET aarch64-managed-timer-wfi-test)
    target_link_libraries(aarch64-managed-timer-wfi-test PRIVATE
        arm_gicv3 qemu_arm_arch_timer_mmio)
    foreach(test_case IN ITEMS baseline timer-wake)
        if(test_case STREQUAL "baseline")
            set(timer_ticks 0)
            set(expect_timer_wake false)
        else()
            set(timer_ticks 10000)
            set(expect_timer_wake true)
        endif()
        add_test(
            NAME aarch64-managed-timer-wfi-${test_case}
            COMMAND $<TARGET_FILE:aarch64-managed-timer-wfi-test>
                -p test-bench.inst.managed_start_in_reset_release=true
                -p test-bench.inst.accel=tcg
                -p test-bench.inst.tcg_mode=MULTI
                -p test-bench.inst.sync_policy=multithread-freerunning
                -p test-bench.inst.time_sync_strategy=quantum_keeper
                -p test-bench.gic.num_spi=64
                -p test-bench.gic.redist_region=[1]
                -p test-bench.timer.nr_frames=1
                -p test-bench.timer_ticks=${timer_ticks}
                -p test-bench.expect_timer_wake=${expect_timer_wake}
                -p log_level=0)
        set_tests_properties(aarch64-managed-timer-wfi-${test_case}
                             PROPERTIES TIMEOUT 30)
    endforeach()
endif()
if(UNIX)
    qbox_add_cpu_test_executable(aarch64-managed-uart-fifo-wfi-test
                                 managed-uart-fifo-wfi-test.cc)
    if(TARGET aarch64-managed-uart-fifo-wfi-test)
        target_link_libraries(aarch64-managed-uart-fifo-wfi-test PRIVATE
            arm_gicv3 uart-pl011 char_backend_file)
        add_test(
            NAME aarch64-managed-uart-fifo-active
            COMMAND $<TARGET_FILE:aarch64-managed-uart-fifo-wfi-test>
                -p test-bench.inst.managed_start_in_reset_release=true
                -p test-bench.inst.accel=tcg
                -p test-bench.inst.tcg_mode=MULTI
                -p test-bench.inst.sync_policy=multithread-freerunning
                -p test-bench.inst.time_sync_strategy=quantum_keeper
                -p test-bench.gic.num_spi=64
                -p test-bench.gic.redist_region=[1]
                -p log_level=0)
        set_tests_properties(aarch64-managed-uart-fifo-active
                             PROPERTIES TIMEOUT 30)
        add_test(
            NAME aarch64-managed-uart-fifo-quiescent
            COMMAND $<TARGET_FILE:aarch64-managed-uart-fifo-wfi-test>
                -p test-bench.inst.managed_start_in_reset_release=true
                -p test-bench.inst.accel=tcg
                -p test-bench.inst.tcg_mode=MULTI
                -p test-bench.inst.sync_policy=multithread-freerunning
                -p test-bench.inst.time_sync_strategy=quantum_keeper
                -p test-bench.gic.num_spi=64
                -p test-bench.gic.redist_region=[1]
                --async-host-input-after-wfi
                -p log_level=0)
        set_tests_properties(aarch64-managed-uart-fifo-quiescent
                             PROPERTIES TIMEOUT 30)
        add_test(
            NAME aarch64-managed-uart-fifo-closed-writer
            COMMAND $<TARGET_FILE:aarch64-managed-uart-fifo-wfi-test>
                -p test-bench.inst.managed_start_in_reset_release=true
                -p test-bench.inst.accel=tcg
                -p test-bench.inst.tcg_mode=MULTI
                -p test-bench.inst.sync_policy=multithread-freerunning
                -p test-bench.inst.time_sync_strategy=quantum_keeper
                -p test-bench.gic.num_spi=64
                -p test-bench.gic.redist_region=[1]
                --closed-writer-after-wfi
                -p log_level=0)
        set_tests_properties(aarch64-managed-uart-fifo-closed-writer
                             PROPERTIES TIMEOUT 30)
        add_test(
            NAME aarch64-managed-uart-fifo-cancel-eintr
            COMMAND $<TARGET_FILE:aarch64-managed-uart-fifo-wfi-test>
                --cancel-eintr-self-test)
        set_tests_properties(aarch64-managed-uart-fifo-cancel-eintr
                             PROPERTIES TIMEOUT 30)
    endif()
endif()
qbox_add_cpu_test(aarch64-smmu-router-stress-test-v2 100 smmu_router_stress_test_v2.cc)
