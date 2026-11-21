if(NOT BUILD_CLIENT)
    return()
endif()

include(utils/add_git_dependency)
include(utils/set_output_dirs)
include(shared_sources)
include(shared_script)

include(renderer_common)

set(CLIENT_SOURCES
    ${SOURCE_DIR}/client/cl_avi.cpp
    ${SOURCE_DIR}/client/cl_censor.cpp
    ${SOURCE_DIR}/client/cl_cgame.cpp
    ${SOURCE_DIR}/client/cl_consolecmds.cpp
    ${SOURCE_DIR}/client/cl_crosshair.c
    ${SOURCE_DIR}/client/cl_demohits.cpp
    ${SOURCE_DIR}/client/cl_demohud.cpp
    ${SOURCE_DIR}/client/cl_demoindex.cpp
    ${SOURCE_DIR}/client/cl_input.cpp
    ${SOURCE_DIR}/client/cl_invrender.cpp
    ${SOURCE_DIR}/client/cl_keys.cpp
    ${SOURCE_DIR}/client/cl_lagometer.cpp
    ${SOURCE_DIR}/client/cl_main.cpp
    ${SOURCE_DIR}/client/cl_net_chan.cpp
    ${SOURCE_DIR}/client/cl_parse.cpp
    ${SOURCE_DIR}/client/cl_scrn.cpp
    ${SOURCE_DIR}/client/cl_ui.cpp
    ${SOURCE_DIR}/client/cl_uicomp.cpp
    ${SOURCE_DIR}/client/cl_uimenus.cpp
    ${SOURCE_DIR}/client/cl_uimain.cpp
    ${SOURCE_DIR}/client/cl_uiextras.cpp
    ${SOURCE_DIR}/client/cl_uistd.cpp
    ${SOURCE_DIR}/client/cl_uiview3d.cpp
    ${SOURCE_DIR}/client/libmumblelink.c
    ${SOURCE_DIR}/client/snd_codec_mp3.c
    ${SOURCE_DIR}/client/snd_codec_ogg.c
    ${SOURCE_DIR}/client/snd_codec_opus.c
    ${SOURCE_DIR}/client/snd_codec_wav.c
    ${SOURCE_DIR}/client/snd_codec.c
    ${SOURCE_DIR}/client/snd_dma_new.cpp
    ${SOURCE_DIR}/client/snd_info.cpp
    ${SOURCE_DIR}/client/snd_mem_new.cpp
    ${SOURCE_DIR}/client/snd_miles_new.cpp
    ${SOURCE_DIR}/client/snd_host.cpp
    ${SOURCE_DIR}/client/omaha/snd_adpcm_omaha.c
    ${SOURCE_DIR}/client/omaha/snd_category.c
    ${SOURCE_DIR}/client/omaha/snd_hrtf_omaha.cpp
    ${SOURCE_DIR}/client/omaha/snd_omaha.cpp
    ${SOURCE_DIR}/client/omaha/snd_omaha_overlay.cpp
    ${SOURCE_DIR}/client/omaha/snd_render_omaha.cpp
    ${SOURCE_DIR}/client/omaha/snd_render_select.cpp
    ${SOURCE_DIR}/client/omaha/snd_stream_async.cpp
    ${SOURCE_DIR}/client/snd_stream_memory.cpp
    ${SOURCE_DIR}/client/usignal.cpp
    ${SOURCE_DIR}/fgame/bg_voteoptions.cpp
    ${SOURCE_DIR}/sdl/sdl_input.c
    ${SOURCE_DIR}/sdl/sdl_mouse.c
    ${CLIENT_PLATFORM_SOURCES}
)

# Gamespy
list(APPEND CLIENT_SOURCES
	${SOURCE_DIR}/gamespy/cl_gamespy.c
)

file(GLOB_RECURSE UI_SOURCES "${SOURCE_DIR}/uilib/*.c" "${SOURCE_DIR}/uilib/*.cpp")

add_git_dependency(${SOURCE_DIR}/client/cl_console.c)

set(CLIENT_BINARY ${CLIENT_NAME})

list(APPEND CLIENT_DEFINITIONS APP_MODULE)

if(BUILD_STANDALONE)
    list(APPEND CLIENT_DEFINITIONS STANDALONE)
endif()

# the renderer is loaded at run time: renderer_<cl_renderer> (renderer_vulkan)
list(APPEND CLIENT_DEFINITIONS USE_RENDERER_DLOPEN)

if(USE_VOIP)
    list(APPEND CLIENT_DEFINITIONS USE_VOIP)
endif()

if(USE_MUMBLE)
    list(APPEND CLIENT_DEFINITIONS USE_MUMBLE)
    list(APPEND CLIENT_LIBRARY_SOURCES ${SOURCE_DIR}/client/libmumblelink.c)
endif()

if(USE_DISCORD)
    list(APPEND CLIENT_DEFINITIONS USE_DISCORD)
    list(APPEND CLIENT_LIBRARY_SOURCES ${SOURCE_DIR}/client/cl_discord.c)
endif()

# the Competitive HUD's menus, faces, pictures, outlines and shader script
# (cg_hudstyle 1) compiled in: the files the filesystem reads when no file on
# disk or in a pak has the name (cl_uicomp.cpp, FS_SetBuiltinFiles); any .svg
# under ui/
file(GLOB BUILTIN_FILES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/assets/main/ui/*.urc
    ${CMAKE_SOURCE_DIR}/assets/main/ui/competitive/*.png
    ${CMAKE_SOURCE_DIR}/assets/main/fonts/*
    ${CMAKE_SOURCE_DIR}/assets/main/scripts/*.shader)
file(GLOB_RECURSE BUILTIN_SVG CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/main/ui/*.svg)
list(APPEND BUILTIN_FILES ${BUILTIN_SVG})
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/builtin_files.c
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}/assets/main -DOUT=${CMAKE_BINARY_DIR}/builtin_files.c
        "-DFILES=${BUILTIN_FILES}" -P ${CMAKE_SOURCE_DIR}/cmake/utils/embed_files.cmake
    DEPENDS ${BUILTIN_FILES} ${CMAKE_SOURCE_DIR}/cmake/utils/embed_files.cmake
    COMMENT "Compiling in the Competitive HUD's files"
    VERBATIM)
list(APPEND CLIENT_SOURCES ${CMAKE_BINARY_DIR}/builtin_files.c)

list(APPEND CLIENT_BINARY_SOURCES
    ${SERVER_SOURCES}
    ${CLIENT_SOURCES}
    ${UI_SOURCES}
    ${COMMON_SOURCES}
    ${SCRIPT_SYSTEM_SOURCES}
    ${SYSTEM_SOURCES}
    ${CLIENT_LIBRARY_SOURCES})

add_executable(${CLIENT_BINARY} ${CLIENT_EXECUTABLE_OPTIONS} ${CLIENT_BINARY_SOURCES})

target_include_directories(     ${CLIENT_BINARY} PRIVATE ${CLIENT_INCLUDE_DIRS})
target_include_directories(     ${CLIENT_BINARY} PRIVATE ${SOURCE_DIR}/client)
target_compile_definitions(     ${CLIENT_BINARY} PRIVATE ${CLIENT_DEFINITIONS})
target_compile_options(         ${CLIENT_BINARY} PRIVATE ${CLIENT_COMPILE_OPTIONS})
target_link_libraries(          ${CLIENT_BINARY} PRIVATE ${COMMON_LIBRARIES} ${CLIENT_LIBRARIES})
target_link_options(            ${CLIENT_BINARY} PRIVATE ${CLIENT_LINK_OPTIONS})

set_output_dirs(${CLIENT_BINARY})

# Ship the headphone HRTF set next to the client binary (fs_basepath / main).
# OMAHA-SOUND-LICENSES.txt is the binary-form notice required by libmysofa,
# the Speex resampler, kdtree, and PFFFT. The SADIE Apache text goes with the data.
add_custom_command(TARGET ${CLIENT_BINARY} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/main/sound
        $<TARGET_FILE_DIR:${CLIENT_BINARY}>/main/sound
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/code/thirdparty/OMAHA-SOUND-LICENSES.txt
        $<TARGET_FILE_DIR:${CLIENT_BINARY}>/OMAHA-SOUND-LICENSES.txt
    COMMENT "Copy sound assets and license notices"
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/main/sound
    DESTINATION ${INSTALL_BINDIR_FULL}/main)
install(FILES ${CMAKE_SOURCE_DIR}/code/thirdparty/OMAHA-SOUND-LICENSES.txt
    DESTINATION ${INSTALL_BINDIR_FULL})

foreach(LIBRARY IN LISTS CLIENT_DEPLOY_LIBRARIES)
    add_custom_command(TARGET ${CLIENT_BINARY} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${LIBRARY}
            $<TARGET_FILE_DIR:${CLIENT_BINARY}>)

    install(FILES ${LIBRARY} DESTINATION
        # install() requires a relative path hence:
        $<PATH:RELATIVE_PATH,$<TARGET_FILE_DIR:${CLIENT_BINARY}>,${CMAKE_BINARY_DIR}/$<CONFIG>>)
endforeach()

set_target_properties(${CLIENT_BINARY} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})

INSTALL(TARGETS ${CLIENT_BINARY} DESTINATION ${INSTALL_BINDIR_FULL})

if (MSVC)
	target_link_options(${CLIENT_BINARY} PRIVATE "/MANIFEST:NO")
	INSTALL(FILES $<TARGET_PDB_FILE:${CLIENT_BINARY}> DESTINATION ${INSTALL_BINDIR_FULL} OPTIONAL)
endif()

# Added in OPM
#  mohdemoindex: indexes demos without the game, for the replay app
add_executable(mohdemoindex
    ${SOURCE_DIR}/client/cl_demoindex_tool.cpp
    ${SOURCE_DIR}/client/cl_demoindex.cpp
    ${SOURCE_DIR}/qcommon/msg.cpp
    ${SOURCE_DIR}/qcommon/huffman.cpp
    ${SOURCE_DIR}/qcommon/bg_compat.cpp
    ${SOURCE_DIR}/qcommon/q_shared.c
    ${SOURCE_DIR}/qcommon/q_math.c
    ${SOURCE_DIR}/qcommon/common_light.c
)

set_output_dirs(mohdemoindex)

INSTALL(TARGETS mohdemoindex DESTINATION ${INSTALL_BINDIR_FULL})
