add_executable(rse_atu-tests rse_atu-tests.cc)
target_link_libraries(rse_atu-tests PRIVATE gtest gmock rse_atu ${TARGET_LIBS})
add_test(NAME rse_atu-tests COMMAND rse_atu-tests)
set_tests_properties(rse_atu-tests PROPERTIES TIMEOUT 20)
