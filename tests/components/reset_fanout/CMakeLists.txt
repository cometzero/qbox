add_executable(reset_fanout-tests reset_fanout-tests.cc)
target_link_libraries(reset_fanout-tests PRIVATE gtest gmock reset_fanout ${TARGET_LIBS})
add_test(NAME reset_fanout-tests COMMAND reset_fanout-tests)
set_tests_properties(reset_fanout-tests PROPERTIES TIMEOUT 20)
