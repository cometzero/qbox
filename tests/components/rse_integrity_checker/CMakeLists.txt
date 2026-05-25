add_executable(rse_integrity_checker-tests rse_integrity_checker-tests.cc)
target_link_libraries(rse_integrity_checker-tests PRIVATE gtest gmock rse_integrity_checker ${TARGET_LIBS})
add_test(NAME rse_integrity_checker-tests COMMAND rse_integrity_checker-tests)
set_tests_properties(rse_integrity_checker-tests PROPERTIES TIMEOUT 20)
