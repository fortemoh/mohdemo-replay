#
# pm_flags must reach AA 1.11 clients in their own bit layout (crouch box)
#

add_executable(test_pm_flags
    ${SOURCE_DIR}/qcommon/tests/test_pm_flags.cpp
    ${SOURCE_DIR}/qcommon/q_shared.c
    ${SOURCE_DIR}/qcommon/common_light.c
)

target_include_directories(test_pm_flags PRIVATE
    ${SOURCE_DIR}
    ${SOURCE_DIR}/qcommon
)

add_test(NAME test_pm_flags COMMAND test_pm_flags)
set_tests_properties(test_pm_flags PROPERTIES TIMEOUT 15)
