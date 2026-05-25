set(ARM_NONE_EABI_HINT
    "/build/arm/arm-auto-solutions/build/tmp_baremetal/sysroots-components/x86_64/gcc-arm-none-eabi-native/usr/libexec/gcc-arm-none-eabi-13.3.rel1/bin"
)

find_program(ARM_NONE_EABI_GCC arm-none-eabi-gcc HINTS ${ARM_NONE_EABI_HINT})
find_program(ARM_NONE_EABI_OBJCOPY arm-none-eabi-objcopy HINTS ${ARM_NONE_EABI_HINT})

if(ARM_NONE_EABI_GCC AND ARM_NONE_EABI_OBJCOPY)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cortex-m55-dmi-byte-store.elf
        COMMAND ${ARM_NONE_EABI_GCC}
                -mcpu=cortex-m55
                -mthumb
                -nostdlib
                -Wl,--build-id=none
                -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/cortex-m55-dmi-byte-store.ld
                -o ${CMAKE_CURRENT_BINARY_DIR}/cortex-m55-dmi-byte-store.elf
                ${CMAKE_CURRENT_SOURCE_DIR}/cortex-m55-dmi-byte-store.S
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cortex-m55-dmi-byte-store.S
                ${CMAKE_CURRENT_SOURCE_DIR}/cortex-m55-dmi-byte-store.ld
        COMMENT "Building cortex-m55-dmi-byte-store firmware"
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cortex-m55-dmi-byte-store.bin
        COMMAND ${ARM_NONE_EABI_OBJCOPY}
                -O binary
                ${CMAKE_CURRENT_BINARY_DIR}/cortex-m55-dmi-byte-store.elf
                ${CMAKE_CURRENT_BINARY_DIR}/cortex-m55-dmi-byte-store.bin
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/cortex-m55-dmi-byte-store.elf
        COMMENT "Creating cortex-m55-dmi-byte-store binary"
    )

    add_custom_target(cortex-m55-dmi-byte-store-firmware
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/cortex-m55-dmi-byte-store.bin)

    qbox_add_cpu_test(cortex-m55-dmi-byte-store-test 30 cortex-m55-dmi-byte-store-test.cc)
    add_dependencies(cortex-m55-dmi-byte-store-test cortex-m55-dmi-byte-store-firmware)
    target_link_libraries(cortex-m55-dmi-byte-store-test PRIVATE cpu_arm_cortexM55)
    target_include_directories(cortex-m55-dmi-byte-store-test PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components>)
    target_compile_definitions(cortex-m55-dmi-byte-store-test PRIVATE
        FIRMWARE_BIN_PATH="${CMAKE_CURRENT_BINARY_DIR}/cortex-m55-dmi-byte-store.bin")
else()
    message(WARNING "arm-none-eabi tools not found - skipping cortex-m55-dmi-byte-store-test")
endif()
