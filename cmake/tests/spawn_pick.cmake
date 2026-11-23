#
# Spawnpoints must be picked as randomly as stock AA 1.11 picks them
#

add_executable(test_spawn_pick
    ${SOURCE_DIR}/fgame/tests/test_spawn_pick.cpp
)

add_test(NAME test_spawn_pick COMMAND test_spawn_pick)
set_tests_properties(test_spawn_pick PROPERTIES TIMEOUT 15)
