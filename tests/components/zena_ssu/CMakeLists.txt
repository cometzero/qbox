add_executable(zena_ssu-tests zena_ssu-tests.cc)
target_link_libraries(zena_ssu-tests PRIVATE gtest gmock zena_ssu ${TARGET_LIBS})
add_test(NAME zena_ssu-tests COMMAND zena_ssu-tests)
set_tests_properties(zena_ssu-tests PROPERTIES TIMEOUT 20)
