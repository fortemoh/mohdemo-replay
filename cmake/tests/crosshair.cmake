#
# crosshair_import / crosshair_export: CS2's crosshair share codes
#

add_executable(test_crosshair
    ${SOURCE_DIR}/client/tests/test_crosshair.c
    ${SOURCE_DIR}/client/cl_crosshair.c
    ${SOURCE_DIR}/qcommon/q_shared.c
    ${SOURCE_DIR}/qcommon/common_light.c
)

# libm on Unix; Windows has the math functions in its C runtime (no m.lib)
if(NOT WIN32)
    target_link_libraries(test_crosshair PRIVATE m)
endif()
add_test(NAME test_crosshair COMMAND test_crosshair)
set_tests_properties(test_crosshair PROPERTIES TIMEOUT 15)
