gs_create_dymod(cpu_arm_cortexM55)

target_include_directories(
    cpu_arm_cortexM55 PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/systemc-components/cc3xx/include>
  )
