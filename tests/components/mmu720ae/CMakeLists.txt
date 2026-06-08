add_executable(mmu720ae-register-tests mmu720ae-register-tests.cc)
target_link_libraries(mmu720ae-register-tests PRIVATE gtest gmock mmu720ae ${TARGET_LIBS})
add_test(NAME mmu720ae-register-tests COMMAND mmu720ae-register-tests)
set_tests_properties(mmu720ae-register-tests PROPERTIES TIMEOUT 20)

add_executable(mmu720ae-queue-tests mmu720ae-queue-tests.cc)
target_link_libraries(mmu720ae-queue-tests PRIVATE gtest gmock mmu720ae ${TARGET_LIBS})
add_test(NAME mmu720ae-queue-tests COMMAND mmu720ae-queue-tests)
set_tests_properties(mmu720ae-queue-tests PROPERTIES TIMEOUT 20)

add_executable(mmu720ae-tbu-tests mmu720ae-tbu-tests.cc)
target_link_libraries(mmu720ae-tbu-tests PRIVATE gtest gmock mmu720ae ${TARGET_LIBS})
add_test(NAME mmu720ae-tbu-tests COMMAND mmu720ae-tbu-tests)
set_tests_properties(mmu720ae-tbu-tests PROPERTIES TIMEOUT 20)
