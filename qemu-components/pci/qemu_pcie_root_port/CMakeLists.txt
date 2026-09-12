gs_create_dymod(qemu_pcie_root_port)

target_include_directories(
    qemu_pcie_root_port PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components/pci/qemu_gpex/include>
  )
