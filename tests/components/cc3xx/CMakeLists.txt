add_executable(cc3xx-tests cc3xx-tests.cc)
target_link_libraries(cc3xx-tests PRIVATE gtest gmock cc3xx ${TARGET_LIBS})
add_test(NAME cc3xx-tests COMMAND cc3xx-tests)
set_tests_properties(cc3xx-tests PROPERTIES TIMEOUT 20)

add_executable(cc3xx_core-tests cc3xx_core-tests.cc)
target_include_directories(cc3xx_core-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/systemc-components/cc3xx/include)
target_link_libraries(cc3xx_core-tests PRIVATE gtest gmock ${TARGET_LIBS})
add_test(NAME cc3xx_core-tests COMMAND cc3xx_core-tests)
set_tests_properties(cc3xx_core-tests PROPERTIES TIMEOUT 20)

add_executable(qemu_cc3xx-tests qemu_cc3xx-tests.cc)
target_include_directories(qemu_cc3xx-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/qemu-components/cc3xx_native/include
    ${PROJECT_SOURCE_DIR}/systemc-components/cc3xx/include)
target_link_libraries(qemu_cc3xx-tests PRIVATE gtest gmock qemu_cc3xx ${TARGET_LIBS})
add_test(NAME qemu_cc3xx-tests COMMAND qemu_cc3xx-tests)
set_tests_properties(qemu_cc3xx-tests PROPERTIES TIMEOUT 20)
