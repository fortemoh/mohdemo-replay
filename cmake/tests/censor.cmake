#
# What cl_censor stars out of names and messages (see test_censor.cpp)
#

add_executable(test_censor
    ${SOURCE_DIR}/client/tests/test_censor.cpp
    ${SOURCE_DIR}/client/cl_censor.cpp
    ${SOURCE_DIR}/qcommon/q_shared.c
    ${SOURCE_DIR}/qcommon/q_math.c
    ${SOURCE_DIR}/qcommon/common_light.c
)

add_test(NAME test_censor COMMAND test_censor)
