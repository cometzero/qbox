add_executable(dw-apb-i2c-tests dw-apb-i2c-tests.cc)
target_link_libraries(dw-apb-i2c-tests PRIVATE gtest gmock dw-apb-i2c ${TARGET_LIBS})
add_test(NAME dw-apb-i2c-tests COMMAND dw-apb-i2c-tests)
set_tests_properties(dw-apb-i2c-tests PROPERTIES TIMEOUT 30)

add_executable(pca9539-tests pca9539-tests.cc)
target_link_libraries(pca9539-tests PRIVATE gtest gmock dw-apb-i2c i2c-bus pca9539 ${TARGET_LIBS})
add_test(NAME pca9539-tests COMMAND pca9539-tests)
set_tests_properties(pca9539-tests PROPERTIES TIMEOUT 30)
