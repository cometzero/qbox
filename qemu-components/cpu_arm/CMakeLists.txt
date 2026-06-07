add_subdirectory(cpu_arm_cortex_a53)
add_subdirectory(cpu_arm_cortex_a55)
add_subdirectory(cpu_arm_cortex_a76)
add_subdirectory(cpu_arm_cortex_a710)
add_subdirectory(cpu_arm_cortex_a720ae)
add_subdirectory(cpu_arm_cortex_m7)
add_subdirectory(cpu_arm_cortex_m55)
add_subdirectory(cpu_arm_cortex_r5)
add_subdirectory(cpu_arm_cortex_r52)
add_subdirectory(cpu_arm_cortex_r82)
add_subdirectory(cpu_arm_neoverse_n1)
add_subdirectory(cpu_arm_neoverse_n2)
add_subdirectory(cpu_arm_host)

foreach(cpu_arm_target
    cpu_arm_cortexA53
    cpu_arm_cortexA55
    cpu_arm_cortexA76
    cpu_arm_cortexA710
    cpu_arm_cortexA720AE
    cpu_arm_cortexM7
    cpu_arm_cortexM55
    cpu_arm_cortexR5
    cpu_arm_cortexR52
    cpu_arm_cortexR82
    cpu_arm_neoverseN1
    cpu_arm_neoverseN2
    cpu_arm_host)
    if(TARGET ${cpu_arm_target})
        target_include_directories(
            ${cpu_arm_target} PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/systemc-components/cc3xx/include>)
    endif()
endforeach()
