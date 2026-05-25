add_executable(rse_sam-tests rse_sam-tests.cc)
target_link_libraries(rse_sam-tests PRIVATE gtest gmock rse_sam ${TARGET_LIBS})
add_test(NAME rse_sam-tests COMMAND rse_sam-tests)
set_tests_properties(rse_sam-tests PROPERTIES TIMEOUT 20)
