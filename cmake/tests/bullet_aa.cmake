#
# Allied Assault bullets must follow stock AA 1.11's BulletAttack rules
#

add_executable(test_bullet_aa
    ${SOURCE_DIR}/fgame/tests/test_bullet_aa.cpp
)

add_test(NAME test_bullet_aa COMMAND test_bullet_aa)
set_tests_properties(test_bullet_aa PROPERTIES TIMEOUT 15)
