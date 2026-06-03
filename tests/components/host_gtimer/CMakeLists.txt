add_executable(host_gtimer-tests host_gtimer-tests.cc)
target_link_libraries(host_gtimer-tests PRIVATE gtest gmock host_gtimer ${TARGET_LIBS})
add_test(NAME host_gtimer-tests COMMAND host_gtimer-tests)
set_tests_properties(host_gtimer-tests PROPERTIES TIMEOUT 20)
