#
# A player getting onto a ladder must be put where stock 1.11 puts them
#

add_executable(test_ladder
    ${SOURCE_DIR}/fgame/tests/test_ladder.cpp
)

add_test(NAME test_ladder COMMAND test_ladder)
set_tests_properties(test_ladder PROPERTIES TIMEOUT 15)
