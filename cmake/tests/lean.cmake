#
# A held lean must step and stop as stock AA 1.11's pmove does (prediction)
#

add_executable(test_lean
    ${SOURCE_DIR}/fgame/tests/test_lean.cpp
)

add_test(NAME test_lean COMMAND test_lean)
set_tests_properties(test_lean PROPERTIES TIMEOUT 15)
