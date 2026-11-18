# SPDX-License-Identifier: GPL-3.0-or-later
# Whether an executable carries the player console's F8 bar (docs/guides/console.md#front-end), from a string only the
# bar puts in it: its settings file's name, `coney-console.ini` (src/platform/console_ui.cpp). The twin of
# debug_tools_markers.cmake's `absent` check: the release build holds no debug tools and does hold the console's bar.
# Run as a script by ctest:
#
#   cmake -DEXE=<executable> -P console_bar_marker.cmake
#
# Fails when the name is not found. Prints whether it was, never anything else.
if(NOT DEFINED EXE)
    message(FATAL_ERROR "usage: cmake -DEXE=<executable> -P console_bar_marker.cmake")
endif()
if(NOT EXISTS "${EXE}")
    message(FATAL_ERROR "${EXE}: no such file")
endif()
file(STRINGS "${EXE}" _found REGEX "coney-console\.ini")
if(NOT _found)
    message(FATAL_ERROR "${EXE} holds no console bar: coney-console.ini not found")
endif()
message(STATUS "the console bar is in ${EXE}")
