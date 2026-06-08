add_executable(mhu320ae-tests mhu320ae-tests.cc)
target_link_libraries(mhu320ae-tests PRIVATE gtest gmock mhu320ae ${TARGET_LIBS})
add_test(NAME mhu320ae-tests COMMAND mhu320ae-tests)
set_tests_properties(mhu320ae-tests PROPERTIES TIMEOUT 20)
