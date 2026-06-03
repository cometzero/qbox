add_executable(host_cmn_cyprus-tests host_cmn_cyprus-tests.cc)
target_link_libraries(host_cmn_cyprus-tests PRIVATE gtest gmock host_cmn_cyprus ${TARGET_LIBS})
add_test(NAME host_cmn_cyprus-tests COMMAND host_cmn_cyprus-tests)
set_tests_properties(host_cmn_cyprus-tests PROPERTIES TIMEOUT 20)
