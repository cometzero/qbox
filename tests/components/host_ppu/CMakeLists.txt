add_executable(host_ppu-tests host_ppu-tests.cc)
target_link_libraries(host_ppu-tests PRIVATE gtest gmock host_ppu ${TARGET_LIBS})
add_test(NAME host_ppu-tests COMMAND host_ppu-tests)
set_tests_properties(host_ppu-tests PROPERTIES TIMEOUT 20)
