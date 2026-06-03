add_executable(host_smcf_mgi-tests host_smcf_mgi-tests.cc)
target_link_libraries(host_smcf_mgi-tests PRIVATE gtest gmock host_smcf_mgi ${TARGET_LIBS})
add_test(NAME host_smcf_mgi-tests COMMAND host_smcf_mgi-tests)
set_tests_properties(host_smcf_mgi-tests PROPERTIES TIMEOUT 20)
