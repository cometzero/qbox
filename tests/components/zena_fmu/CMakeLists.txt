add_executable(zena_fmu-tests zena_fmu-tests.cc)
target_link_libraries(zena_fmu-tests PRIVATE gtest gmock zena_fmu ${TARGET_LIBS})
add_test(NAME zena_fmu-tests COMMAND zena_fmu-tests)
set_tests_properties(zena_fmu-tests PROPERTIES TIMEOUT 20)
