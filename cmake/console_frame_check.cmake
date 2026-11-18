# SPDX-License-Identifier: GPL-3.0-or-later
# Closed means untouched (docs/guides/studio/console.md#tests): the last frame of a windowed test-mode run that opens
# the console's bar for its first frames (`--console-open`) is byte-identical to the same run without it. Run as a
# script by ctest:
#
#   cmake -DEXE=<coney> -DWORK_DIR=<folder> -P console_frame_check.cmake
#
# The runs need a display and OpenGL 3.3: when the run without the bar cannot make its picture, the check prints
# `console frame check skipped` (the test's SKIP_REGULAR_EXPRESSION) instead of failing.
if(NOT DEFINED EXE OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "usage: cmake -DEXE=<coney> -DWORK_DIR=<folder> -P console_frame_check.cmake")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(_common --frames 60 --no-activate)
execute_process(COMMAND "${EXE}" ${_common} --screenshot "${WORK_DIR}/without.png" RESULT_VARIABLE _without
                OUTPUT_QUIET ERROR_VARIABLE _without_err)
if(NOT _without EQUAL 0 OR NOT EXISTS "${WORK_DIR}/without.png")
    message(STATUS "console frame check skipped: no picture without the bar (${_without}): ${_without_err}")
    return()
endif()
execute_process(COMMAND "${EXE}" ${_common} --console-open 30 --screenshot "${WORK_DIR}/with.png"
                RESULT_VARIABLE _with OUTPUT_QUIET ERROR_VARIABLE _with_err)
if(NOT _with EQUAL 0 OR NOT EXISTS "${WORK_DIR}/with.png")
    message(FATAL_ERROR "the run with --console-open made no picture (${_with}): ${_with_err}")
endif()
file(SHA256 "${WORK_DIR}/without.png" _a)
file(SHA256 "${WORK_DIR}/with.png" _b)
if(NOT _a STREQUAL _b)
    message(FATAL_ERROR "the last frame differs once the console bar has closed")
endif()
message(STATUS "the last frame is the same with the console bar opened and closed")
