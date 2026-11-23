#
# Crates must wobble, settle and take damage as stock 1.11's func_crate
#

add_executable(test_crate
    ${SOURCE_DIR}/fgame/tests/test_crate.cpp
)

add_test(NAME test_crate COMMAND test_crate)
set_tests_properties(test_crate PROPERTIES TIMEOUT 15)
