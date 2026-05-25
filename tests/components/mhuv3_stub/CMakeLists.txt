add_executable(mhuv3_stub-tests mhuv3_stub-tests.cc)
target_link_libraries(mhuv3_stub-tests PRIVATE gtest gmock mhuv3_stub ${TARGET_LIBS})
add_test(NAME mhuv3_stub-tests COMMAND mhuv3_stub-tests)
set_tests_properties(mhuv3_stub-tests PROPERTIES TIMEOUT 20)
