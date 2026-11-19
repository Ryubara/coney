# SPDX-License-Identifier: GPL-3.0-or-later
# Closed means untouched (docs/guides/studio/console.md#tests): the last frame of a windowed test-mode run that opens
# the console's bar for its first frames (`--console-open`) is byte-identical to the same run without it. Run as a
# script by ctest:
#
#   cmake -DEXE=<coney> -DWORK_DIR=<folder> -P console_frame_check.cmake
#
# The runs need a display and OpenGL 3.3: when the run without the bar cannot make its picture, or makes a different one
# each time (a window nobody sees can leave the frame buffer's pixels undefined, as on a macOS runner with no screen),
# the check prints `console frame check skipped` (the test's SKIP_REGULAR_EXPRESSION) instead of failing.
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
# The same run again: a picture that is not repeatable shows nothing about the bar.
execute_process(COMMAND "${EXE}" ${_common} --screenshot "${WORK_DIR}/again.png" RESULT_VARIABLE _again
                OUTPUT_QUIET ERROR_VARIABLE _again_err)
if(NOT _again EQUAL 0 OR NOT EXISTS "${WORK_DIR}/again.png")
    message(STATUS "console frame check skipped: no picture on the repeat run (${_again}): ${_again_err}")
    return()
endif()
file(SHA256 "${WORK_DIR}/without.png" _first)
file(SHA256 "${WORK_DIR}/again.png" _repeat)
if(NOT _first STREQUAL _repeat)
    message(STATUS "console frame check skipped: two runs without the bar make different pictures (${_first} ${_repeat})")
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
    message(FATAL_ERROR "the last frame differs once the console bar has closed (${_a} without, ${_b} with)")
endif()
message(STATUS "the last frame is the same with the console bar opened and closed")
