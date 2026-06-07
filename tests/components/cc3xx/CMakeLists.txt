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

add_executable(rse_lms_accel-tests rse_lms_accel-tests.cc)
target_include_directories(rse_lms_accel-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/qemu-components/common/include)
target_link_libraries(rse_lms_accel-tests PRIVATE gtest_main gmock)
add_test(NAME rse_lms_accel-tests COMMAND rse_lms_accel-tests)
set_tests_properties(rse_lms_accel-tests PROPERTIES TIMEOUT 20)

add_executable(rse_mcuboot_image-tests rse_mcuboot_image-tests.cc)
target_include_directories(rse_mcuboot_image-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/qemu-components/common/include)
target_link_libraries(rse_mcuboot_image-tests PRIVATE gtest_main gmock)
add_test(NAME rse_mcuboot_image-tests COMMAND rse_mcuboot_image-tests)
set_tests_properties(rse_mcuboot_image-tests PROPERTIES TIMEOUT 20)

add_executable(rse_p256_ecdsa-tests rse_p256_ecdsa-tests.cc)
target_include_directories(rse_p256_ecdsa-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/qemu-components/common/include)
target_link_libraries(rse_p256_ecdsa-tests PRIVATE gtest_main gmock)
add_test(NAME rse_p256_ecdsa-tests COMMAND rse_p256_ecdsa-tests)
set_tests_properties(rse_p256_ecdsa-tests PROPERTIES TIMEOUT 20)
