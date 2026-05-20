add_executable(cc3xx-tests cc3xx-tests.cc)
target_link_libraries(cc3xx-tests PRIVATE gtest gmock cc3xx ${TARGET_LIBS})
add_test(NAME cc3xx-tests COMMAND cc3xx-tests)
set_tests_properties(cc3xx-tests PROPERTIES TIMEOUT 20)
