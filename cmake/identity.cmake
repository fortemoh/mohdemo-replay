# The programs' names (and Linux's install folder, bin/<PROJECT_NAME>).
# Changed in OPM: kept as they were here (MoHAA Reloaded's are mohreloaded and
# mohreloaded-server): the replay app, its tests and the installs made of it
# start openmohaa
set(PROJECT_NAME openmohaa)
# MoHAA Reloaded's version, which the game shows and sends (cmake/version.cmake adds the stage, the CI run and the
# commit). A release's tag is v<this>: CI's tag builds check it (PRODUCT_RELEASE_TAG, below).
set(PROJECT_VERSION 1.0.0)

set(SERVER_NAME omohaaded)
set(CLIENT_NAME openmohaa)

set(BASEGAME)

set(CGAME_MODULE cgame)
set(GAME_MODULE game)
set(UI_MODULE ui)

set(WINDOWS_ICON_PATH ${CMAKE_SOURCE_DIR}/misc/windows/openmohaa.ico)

set(COPYRIGHT "OpenMoHAA Copyright © 2025 OpenMoHAA Team, Inc. All rights reserved.")

set(CONTACT_EMAIL "")
set(PROTOCOL_HANDLER_SCHEME openmohaa)
