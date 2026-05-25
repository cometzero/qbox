add_executable(rse_kmu-tests rse_kmu-tests.cc)
target_link_libraries(rse_kmu-tests PRIVATE gtest gmock rse_kmu ${TARGET_LIBS})
add_test(NAME rse_kmu-tests COMMAND rse_kmu-tests)
set_tests_properties(rse_kmu-tests PROPERTIES TIMEOUT 20)
