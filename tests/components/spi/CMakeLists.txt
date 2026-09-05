add_executable(dw-apb-ssi-tests dw-apb-ssi-tests.cc)
target_link_libraries(dw-apb-ssi-tests PRIVATE gtest gmock dw-apb-ssi ${TARGET_LIBS})
add_test(NAME dw-apb-ssi-tests COMMAND dw-apb-ssi-tests)
set_tests_properties(dw-apb-ssi-tests PROPERTIES TIMEOUT 30)
