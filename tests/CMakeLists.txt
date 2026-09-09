# Place test executables under <build>/tests/... mirroring the source tree,
# rather than in the build root.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "")

# On Windows there is no RPATH, so test executables need the build root on
# PATH in order to find qbox.dll and libqemu.
if(WIN32)
    set(_test_env_mod "PATH=path_list_prepend:${CMAKE_BINARY_DIR}")
    macro(add_test)
        _add_test(${ARGN})
        cmake_parse_arguments(_gs_at "" "NAME" "" ${ARGN})
        if(_gs_at_NAME)
            set_tests_properties(${_gs_at_NAME} PROPERTIES
                ENVIRONMENT_MODIFICATION "${_test_env_mod}")
        endif()
    endmacro()
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/QBoxSelectedTests.cmake")
if(WITHOUT_QEMU)
    list(REMOVE_ITEM QBOX_CORE_TEST_DIRS qbox)
endif()
qbox_add_selected_test_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
    qbox_core_test_targets
    ${QBOX_CORE_TEST_DIRS}
)
add_custom_target(qbox_core_unit_tests DEPENDS ${qbox_core_test_targets})
