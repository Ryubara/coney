# SPDX-License-Identifier: GPL-3.0-or-later
# Whether an executable holds the debug tools (docs/guides/building.md#debug-tools), from strings only they put in it:
# the debug pages' titles and items and the developer overlay's settings file. Dear ImGui's own default settings file
# name (`imgui.ini`) is not a marker: the release build's player console links Dear ImGui and so holds it. A marker
# must not appear in a source the release build links, the command-line help (src/core/options.cpp) and the player
# console's text included. Run as a script by ctest:
#
#   cmake -DEXE=<executable> -DEXPECT=absent|present -P debug_tools_markers.cmake
#
# `absent` (the release build's entry point) fails when any marker is found; `present` (the debug tools' `coney`)
# fails when none is, so the first check cannot pass by finding nothing. Prints the markers found, never anything else.
if(NOT DEFINED EXE OR NOT DEFINED EXPECT)
    message(FATAL_ERROR "usage: cmake -DEXE=<executable> -DEXPECT=absent|present -P debug_tools_markers.cmake")
endif()
if(NOT EXISTS "${EXE}")
    message(FATAL_ERROR "${EXE}: no such file")
endif()

set(_markers "Entity inspector" "Entity editor" "Scale collision with mesh" "Lua console" "Animation player"
            "coney-imgui.ini")
list(JOIN _markers "|" _pattern)
string(REPLACE "." "\\." _pattern "${_pattern}")
file(STRINGS "${EXE}" _found REGEX "${_pattern}")
set(_hits "")
foreach(_marker IN LISTS _markers)
    foreach(_line IN LISTS _found)
        string(FIND "${_line}" "${_marker}" _at)
        if(NOT _at EQUAL -1)
            list(APPEND _hits "${_marker}")
            break()
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _hits)
list(LENGTH _hits _count)

if(EXPECT STREQUAL "absent")
    if(_count GREATER 0)
        message(FATAL_ERROR "${EXE} holds the debug tools: ${_hits}")
    endif()
    message(STATUS "no debug tools in ${EXE}")
elseif(EXPECT STREQUAL "present")
    if(_count EQUAL 0)
        message(FATAL_ERROR "${EXE}: no debug tools' marker found; the check cannot tell a release build from it")
    endif()
    message(STATUS "debug tools found in ${EXE}: ${_hits}")
else()
    message(FATAL_ERROR "EXPECT must be absent or present, not ${EXPECT}")
endif()
