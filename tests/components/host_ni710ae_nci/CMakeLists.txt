add_executable(host_ni710ae_nci-tests host_ni710ae_nci-tests.cc)
target_link_libraries(host_ni710ae_nci-tests PRIVATE gtest gmock host_ni710ae_nci ${TARGET_LIBS})
add_test(NAME host_ni710ae_nci-tests COMMAND host_ni710ae_nci-tests)
set_tests_properties(host_ni710ae_nci-tests PROPERTIES TIMEOUT 20)
