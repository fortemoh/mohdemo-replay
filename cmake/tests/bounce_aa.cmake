#
# A bouncing grenade must make the sound stock AA 1.11 makes
#

add_executable(test_bounce_aa
    ${SOURCE_DIR}/fgame/tests/test_bounce_aa.cpp
)

add_test(NAME test_bounce_aa COMMAND test_bounce_aa)
set_tests_properties(test_bounce_aa PROPERTIES TIMEOUT 15)
