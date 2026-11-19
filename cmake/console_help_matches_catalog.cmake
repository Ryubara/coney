# SPDX-License-Identifier: GPL-3.0-or-later
# Whether the release executable's console `help` lists exactly the player catalog's commands that are available
# (docs/guides/studio/console.md#release): the done item of the player commands. The expected names are read from
# src/console/commands.toml, the catalog's one source, so a command added there is expected here with no edit; a
# command that has a feature to wait for (`available`) is not, as the game leaves it out until the feature is built.
# Run as a script by ctest:
#
#   cmake -DEXE=<release executable> -DCATALOG=<commands.toml> -DINPUT=<input script with a `console help` line>
#         -P console_help_matches_catalog.cmake
#
# Fails when a name is missing from `help`, when `help` lists a name the catalog does not have available (a developer's,
# a mod's, a command whose feature is not built), or when `help` printed nothing. Prints counts and names only.
# IN_LIST in `if`, as the rest of the build has it.
cmake_policy(SET CMP0057 NEW)

if(NOT DEFINED EXE OR NOT DEFINED CATALOG OR NOT DEFINED INPUT)
    message(FATAL_ERROR "usage: cmake -DEXE=<executable> -DCATALOG=<commands.toml> -DINPUT=<input script> -P "
                        "console_help_matches_catalog.cmake")
endif()

# The names the catalog says a release build has: access player, no `available`. Entries start at `[[command]]`; a line
# `name = "x"`, `access = "y"` or `available = "z"` belongs to the entry it is under. Brackets and semicolons are made
# harmless before the text is split into a list, which they would break (`[[command]]` is then read as `((command))`).
file(READ "${CATALOG}" _text)
string(REPLACE "[" "(" _text "${_text}")
string(REPLACE "]" ")" _text "${_text}")
string(REPLACE ";" "," _text "${_text}")
string(REPLACE "\n" ";" _lines "${_text}")
set(_expected "")
set(_name "")
set(_player FALSE)
set(_waits FALSE)
foreach(_line IN LISTS _lines ITEMS "((command))")
    if(_line STREQUAL "((command))")
        if(NOT _name STREQUAL "" AND _player AND NOT _waits)
            list(APPEND _expected "${_name}")
        endif()
        set(_name "")
        set(_player FALSE)
        set(_waits FALSE)
    elseif(_line MATCHES "^name = \"([a-z][a-z0-9_-]*)\"")
        set(_name "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "^access = \"player\"")
        set(_player TRUE)
    elseif(_line MATCHES "^available = ")
        set(_waits TRUE)
    endif()
endforeach()
list(LENGTH _expected _expected_count)
if(_expected_count EQUAL 0)
    message(FATAL_ERROR "${CATALOG}: no available player command found; the reader is out of date")
endif()

# What `help` printed: the console's lines under its banner that start with a command name (a group's heading starts
# with a capital) and, after two spaces at least, its summary.
execute_process(COMMAND "${EXE}" --headless --frames 5 --input-script "${INPUT}"
                RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "${EXE} exited with ${_result}:\n${_out}${_err}")
endif()
string(REPLACE "\r\n" "\n" _all "${_out}\n${_err}")
# Brackets and semicolons again made harmless for the list (a `[dev]` mark is then `(dev)`).
string(REPLACE "[" "(" _all "${_all}")
string(REPLACE "]" ")" _all "${_all}")
string(REPLACE ";" "," _all "${_all}")
string(REPLACE "\n" ";" _all "${_all}")
set(_listed "")
set(_in_help FALSE)
foreach(_line IN LISTS _all)
    if(_line MATCHES "Coney console: the commands this build has")
        set(_in_help TRUE)
    elseif(_in_help AND _line MATCHES "console: +([a-z][a-z0-9_-]*)(\\([a-z]+\\))?  +[^ ]")
        list(APPEND _listed "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT _in_help)
    message(FATAL_ERROR "`help` printed no banner:\n${_out}${_err}")
endif()

list(SORT _expected)
list(SORT _listed)
list(REMOVE_DUPLICATES _listed)
set(_missing "")
foreach(_wanted IN LISTS _expected)
    if(NOT _wanted IN_LIST _listed)
        list(APPEND _missing "${_wanted}")
    endif()
endforeach()
set(_extra "")
foreach(_found IN LISTS _listed)
    if(NOT _found IN_LIST _expected)
        list(APPEND _extra "${_found}")
    endif()
endforeach()
if(_missing OR _extra)
    message(FATAL_ERROR "help lists [${_listed}] where the catalog has [${_expected}]: missing [${_missing}], "
                        "extra [${_extra}]; the run printed:
${_out}${_err}")
endif()
list(LENGTH _listed _listed_count)
message(STATUS "help lists the ${_listed_count} commands the catalog has available: ${_listed}")
