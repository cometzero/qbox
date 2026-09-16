qbox_add_cpu_test(aarch64-simple-write-test 100 simple-write-test.cc)
qbox_add_cpu_test_executable(aarch64-mcips-irq-wake-test mcips-irq-wake-test.cc)
if(QBOX_ENABLE_MCIPS_TESTS)
  # Native scheduler stress is diagnostic-only: its post-sc_stop teardown
  # can still hang independently of the pause/resume assertions.
  qbox_add_cpu_test_executable(aarch64-mcips-native-pause-race-test mcips-native-pause-race-test.cc)
  add_test(NAME aarch64-mcips-native-pause-race-test
      COMMAND $<TARGET_FILE:aarch64-mcips-native-pause-race-test>
          -p test-bench.inst.accel=tcg -p test-bench.inst.tcg_mode=MULTI
          -p test-bench.inst.time_sync_strategy=quantum_keeper
          -p test-bench.inst.sync_policy=multithread-freerunning -p log_level=0)
  set_tests_properties(aarch64-mcips-native-pause-race-test PROPERTIES TIMEOUT 15)
  foreach(cpu_count IN ITEMS 1 2)
    add_test(NAME aarch64-mcips-irq-wake-${cpu_count}cpu COMMAND $<TARGET_FILE:aarch64-mcips-irq-wake-test>
        -p test-bench.inst.accel=tcg -p test-bench.inst.tcg_mode=MULTI
        -p test-bench.inst.time_sync_strategy=mcips
        -p test-bench.quantum_ns=100000 -p test-bench.num_cpu=${cpu_count} -p log_level=0)
    set_tests_properties(aarch64-mcips-irq-wake-${cpu_count}cpu PROPERTIES TIMEOUT 10)
  endforeach()
  add_test(NAME aarch64-mcips-held-peer-test COMMAND $<TARGET_FILE:aarch64-mcips-irq-wake-test>
      -p test-bench.inst.accel=tcg -p test-bench.inst.tcg_mode=MULTI
      -p test-bench.inst.time_sync_strategy=mcips
      -p test-bench.quantum_ns=100000 -p test-bench.num_cpu=2
      -p test-bench.peer_start_in_reset=true -p log_level=0)
  set_tests_properties(aarch64-mcips-held-peer-test PROPERTIES TIMEOUT 10)
  foreach(wake_case IN ITEMS missed-resume reset-wake exec-entry)
    if(wake_case STREQUAL "missed-resume")
        set(wake_option test-bench.suppress_resume=true)
    elseif(wake_case STREQUAL "exec-entry")
        set(wake_option test-bench.queued_idle=true)
    else()
        set(wake_option test-bench.reset_rounds=20)
    endif()
    add_test(NAME aarch64-mcips-${wake_case}-test COMMAND $<TARGET_FILE:aarch64-mcips-irq-wake-test>
        -p test-bench.inst.accel=tcg -p test-bench.inst.tcg_mode=MULTI
        -p test-bench.inst.time_sync_strategy=mcips
        -p test-bench.quantum_ns=100000 -p ${wake_option} -p log_level=0)
    set_tests_properties(aarch64-mcips-${wake_case}-test PROPERTIES TIMEOUT 10)
  endforeach()
endif()
qbox_add_cpu_test_executable(cortex-m55-wfx-test cortex-m55-wfx-test.cc)
target_link_libraries(cortex-m55-wfx-test PRIVATE cpu_arm_cortexM55)
if(QBOX_ENABLE_MCIPS_TESTS)
    add_test(NAME cortex-m55-wfx-test COMMAND $<TARGET_FILE:cortex-m55-wfx-test>
        -p test-bench.inst.accel=tcg -p test-bench.inst.tcg_mode=MULTI
        -p test-bench.inst.time_sync_strategy=mcips
        -p test-bench.quantum_ns=100000 -p log_level=0)
    set_tests_properties(cortex-m55-wfx-test PROPERTIES TIMEOUT 10)
endif()
qbox_add_cpu_test_executable(aarch64-mcips-timed-mmio-test mcips-timed-mmio-test.cc)
qbox_add_cpu_test_executable(aarch64-mcips-pause-wfi-test mcips-pause-wfi-test.cc)
qbox_add_cpu_test_executable(aarch64-standby-wfi-test standby-wfi-test.cc)
# Keep the standby diagnostics opt-in: the quantum-keeper variant currently
# passes its signal assertions but can hang in libqemu pause_all_vcpus teardown.
if(TARGET aarch64-standby-wfi-test AND QBOX_ENABLE_MCIPS_TESTS)
    add_test(NAME aarch64-standby-reset-feedback
        COMMAND $<TARGET_FILE:aarch64-standby-wfi-test>
            -p test-bench.inst.accel=tcg -p test-bench.inst.tcg_mode=MULTI
            -p test-bench.inst.time_sync_strategy=mcips
            -p test-bench.quantum_ns=100 -p test-bench.reset_feedback=true -p log_level=0)
    set_tests_properties(aarch64-standby-reset-feedback PROPERTIES TIMEOUT 10)
    foreach(strategy IN ITEMS quantum_keeper mcips)
        if(strategy STREQUAL "mcips")
            set(standby_quantum 100)
        else()
            set(standby_quantum 100000)
        endif()
        add_test(NAME aarch64-standby-wfi-${strategy}
            COMMAND $<TARGET_FILE:aarch64-standby-wfi-test>
                -p test-bench.inst.accel=tcg
                -p test-bench.inst.tcg_mode=MULTI
                -p test-bench.inst.time_sync_strategy=${strategy}
                -p test-bench.inst.sync_policy=multithread-quantum
                -p test-bench.quantum_ns=${standby_quantum}
                -p log_level=0)
        set_tests_properties(aarch64-standby-wfi-${strategy} PROPERTIES TIMEOUT 10)
    endforeach()
endif()
if(TARGET aarch64-mcips-pause-wfi-test AND QBOX_ENABLE_MCIPS_TESTS)
    add_test(NAME aarch64-mcips-pause-wfi-test
        COMMAND $<TARGET_FILE:aarch64-mcips-pause-wfi-test>
            -p test-bench.inst.accel=tcg
            -p test-bench.inst.tcg_mode=MULTI
            -p test-bench.inst.time_sync_strategy=mcips
            -p test-bench.quantum_ns=100
            -p log_level=0)
    set_tests_properties(aarch64-mcips-pause-wfi-test PROPERTIES TIMEOUT 10)
endif()
if(TARGET aarch64-mcips-timed-mmio-test AND QBOX_ENABLE_MCIPS_TESTS)
    foreach(cpu_count IN ITEMS 1 2)
      foreach(annotated IN ITEMS false true)
        add_test(
            NAME aarch64-mcips-timed-mmio-${cpu_count}cpu-${annotated}
            COMMAND $<TARGET_FILE:aarch64-mcips-timed-mmio-test>
                -p test-bench.inst.accel=tcg
                -p test-bench.inst.tcg_mode=MULTI
                -p test-bench.inst.time_sync_strategy=mcips
                -p test-bench.num_cpu=${cpu_count}
                -p test-bench.annotated=${annotated}
                -p test-bench.quantum_ns=100000
                -p log_level=0)
        set_tests_properties(aarch64-mcips-timed-mmio-${cpu_count}cpu-${annotated} PROPERTIES TIMEOUT 10)
      endforeach()
    endforeach()
endif()
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
