# SPDX-License-Identifier: GPL-3.0-or-later
# Whether an executable holds a string, for the checks that prove what a build carries (docs/guides/building.md#debug-
# tools): the release build's `coney.release.has_player_console` looks for the console's banner, so the build is proved
# to carry the console, as `coney.debug_tools_markers_found` proves the markers check finds something. Run as a script
# by ctest:
#
#   cmake -DEXE=<executable> -DTEXT=<text> -P executable_has_string.cmake
#
# Fails when TEXT is not in the file. Prints the text it looked for and whether it was found, never anything else.
if(NOT DEFINED EXE OR NOT DEFINED TEXT)
    message(FATAL_ERROR "usage: cmake -DEXE=<executable> -DTEXT=<text> -P executable_has_string.cmake")
endif()
if(NOT EXISTS "${EXE}")
    message(FATAL_ERROR "${EXE}: no such file")
endif()

# The text is looked for as a plain string, so regular expression characters in it count as themselves.
string(REGEX REPLACE "([][+.*()^$?|\\\\])" "\\\\\\1" _pattern "${TEXT}")
file(STRINGS "${EXE}" _found REGEX "${_pattern}" LIMIT_COUNT 1)
if(NOT _found)
    message(FATAL_ERROR "${EXE} does not hold \"${TEXT}\"")
endif()
message(STATUS "${EXE} holds \"${TEXT}\"")
