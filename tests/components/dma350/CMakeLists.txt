add_executable(dma350-tests dma350-tests.cc)
target_link_libraries(dma350-tests PRIVATE gtest gmock dma350 ${TARGET_LIBS})
add_test(NAME dma350-tests COMMAND dma350-tests)
set_tests_properties(dma350-tests PROPERTIES TIMEOUT 20)
