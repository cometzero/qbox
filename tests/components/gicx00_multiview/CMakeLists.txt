add_executable(gicx00_multiview-tests gicx00_multiview-tests.cc)
target_link_libraries(gicx00_multiview-tests PRIVATE gtest gmock gicx00_multiview ${TARGET_LIBS})
add_test(NAME gicx00_multiview-tests COMMAND gicx00_multiview-tests)
set_tests_properties(gicx00_multiview-tests PROPERTIES TIMEOUT 20)
