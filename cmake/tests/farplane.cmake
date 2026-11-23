#
# AA snapshots must drop entities for distance exactly as stock AA 1.11 does
#

add_executable(test_farplane
    ${SOURCE_DIR}/server/tests/test_farplane.cpp
)

add_test(NAME test_farplane COMMAND test_farplane)
set_tests_properties(test_farplane PROPERTIES TIMEOUT 15)
