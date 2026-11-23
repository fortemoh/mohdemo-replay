#
# The game module's masks must be AA 1.11's on an AA server
#

add_executable(test_masks_aa
    ${SOURCE_DIR}/fgame/tests/test_masks_aa.cpp
)

target_include_directories(test_masks_aa PRIVATE
    ${SOURCE_DIR}
    ${SOURCE_DIR}/qcommon
)

add_test(NAME test_masks_aa COMMAND test_masks_aa)
set_tests_properties(test_masks_aa PROPERTIES TIMEOUT 15)
