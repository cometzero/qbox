add_executable(host_scr-tests host_scr-tests.cc)
target_link_libraries(host_scr-tests PRIVATE gtest gmock host_scr ${TARGET_LIBS})
add_test(NAME host_scr-tests COMMAND host_scr-tests)
set_tests_properties(host_scr-tests PROPERTIES TIMEOUT 20)
