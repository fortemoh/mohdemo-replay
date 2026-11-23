#
# Unit tests for Enhanced Rendering's rules (Project: Omaha's): r_temporal on with the effects that need it, they off with it
#

add_executable(test_effects_logic
    ${SOURCE_DIR}/client/tests/test_effects_logic.cpp
)

add_test(NAME test_effects_logic COMMAND test_effects_logic)

set_tests_properties(test_effects_logic PROPERTIES TIMEOUT 15)
