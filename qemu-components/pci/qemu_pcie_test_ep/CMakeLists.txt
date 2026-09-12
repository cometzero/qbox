gs_create_dymod(qemu_pcie_test_ep)

target_include_directories(
    qemu_pcie_test_ep PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components/pci/qemu_gpex/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components/pci/qemu_pcie_root_port/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components/pci/qemu_pcie_epc/include>
  )
