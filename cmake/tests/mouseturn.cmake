#
# Unit tests for the mouse's turn and the turn distance (cm/360) kept when the Field of View changes
#

add_executable(test_mouseturn
    ${SOURCE_DIR}/client/tests/test_mouseturn.cpp
)

# libm on Unix; Windows has the math functions in its C runtime (no m.lib)
if(NOT WIN32)
    target_link_libraries(test_mouseturn PRIVATE m)
endif()
add_test(NAME test_mouseturn COMMAND test_mouseturn)
set_tests_properties(test_mouseturn PROPERTIES TIMEOUT 15)
