#
# Remote prediction's model (code/cgame/remote_predict.cpp) against bg_pmove
#

add_executable(test_remote_predict
    ${SOURCE_DIR}/cgame/tests/test_remote_predict.cpp
    ${SOURCE_DIR}/cgame/remote_predict.cpp
    ${SOURCE_DIR}/fgame/bg_pmove.cpp
    ${SOURCE_DIR}/fgame/bg_slidemove.cpp
    ${SOURCE_DIR}/qcommon/q_math.c
    ${SOURCE_DIR}/qcommon/q_shared.c
    ${SOURCE_DIR}/qcommon/common_light.c
)

target_include_directories(test_remote_predict PRIVATE
    ${SOURCE_DIR}
    ${SOURCE_DIR}/fgame
    ${SOURCE_DIR}/qcommon
)

add_test(NAME test_remote_predict COMMAND test_remote_predict)
set_tests_properties(test_remote_predict PROPERTIES TIMEOUT 15)
