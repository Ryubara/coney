# SPDX-License-Identifier: GPL-3.0-or-later
# Run by the build, in script mode (cmake -P), to compile one HLSL shader to one format with the shadercross tool:
#   cmake -DTOOL=<shadercross> -DSOURCE=<file.hlsl> -DOUTPUT=<file> -DFORMAT=<SPIRV|DXIL|MSL|JSON> -DSTAGE=<vertex|
#         fragment|compute> -P coney_shader_compile.cmake
# Why a script and not the tool alone: the tool opens its output file before it compiles, so a shader that fails to
# compile leaves an empty file newer than its source, and the next build would take that for a finished one. Here the
# output is written under another name and moved into place only when the tool succeeded and wrote something, so a
# failure leaves nothing (and says why, in the tool's own words).
# The project's policies, so this script parses the same under every CMake version (without it CMake 3 reads it with
# its oldest rules, and CMake 4 with its newest).
cmake_minimum_required(VERSION 3.28)
foreach(_var TOOL SOURCE OUTPUT FORMAT STAGE)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "coney_shader_compile.cmake: ${_var} is not set")
    endif()
endforeach()

file(REMOVE "${OUTPUT}" "${OUTPUT}.part")
execute_process(
    COMMAND "${TOOL}" "${SOURCE}" --source HLSL --dest ${FORMAT} --stage ${STAGE} --entrypoint main
            --output "${OUTPUT}.part"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output
    ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE)
if(NOT _result EQUAL 0)
    file(REMOVE "${OUTPUT}.part")
    message(FATAL_ERROR "Compiling ${SOURCE} to ${FORMAT} failed (exit ${_result}).")
endif()
file(SIZE "${OUTPUT}.part" _size)
if(_size EQUAL 0)
    file(REMOVE "${OUTPUT}.part")
    message(FATAL_ERROR "Compiling ${SOURCE} to ${FORMAT} wrote an empty file.")
endif()
file(RENAME "${OUTPUT}.part" "${OUTPUT}")
