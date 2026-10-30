# SPDX-License-Identifier: GPL-3.0-or-later
# Checks, in script mode, that the shader build does not take a shader that fails to compile for one that compiled:
#   cmake -DTOOL=<shadercross> -DFORMAT=<SPIRV|DXIL|MSL> -DCOMPILE_SCRIPT=<cmake/coney_shader_compile.cmake>
#         -DBROKEN=<a shader that does not compile> -DGOOD=<one that does> -DWORK_DIR=<a scratch folder> -P ...
# The compile step must exit non-zero for BROKEN and leave no output file, however the compiler handles its output
# (shadercross creates the file before it compiles); and succeed with a non-empty file for GOOD, so that the refusal is
# the shader's and not the harness's (a missing tool, a wrong argument).
foreach(_var TOOL FORMAT COMPILE_SCRIPT BROKEN GOOD WORK_DIR)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "shader_refusal_check.cmake: ${_var} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# Runs the compile step on `source` and sets `result_variable` to its exit code and `<result_variable>_output` to what
# it printed.
function(run_compile source output result_variable)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DTOOL=${TOOL} -DSOURCE=${source} -DOUTPUT=${output} -DFORMAT=${FORMAT}
                -DSTAGE=fragment -P ${COMPILE_SCRIPT}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _output)
    set(${result_variable} ${_result} PARENT_SCOPE)
    set(${result_variable}_output "${_output}" PARENT_SCOPE)
endfunction()

run_compile("${GOOD}" "${WORK_DIR}/good.out" good_result)
if(NOT good_result EQUAL 0 OR NOT EXISTS "${WORK_DIR}/good.out")
    message(FATAL_ERROR "The control shader ${GOOD} did not compile to ${FORMAT} (${good_result}):\n${good_result_output}")
endif()
file(SIZE "${WORK_DIR}/good.out" _good_size)
if(_good_size EQUAL 0)
    message(FATAL_ERROR "The control shader compiled to ${FORMAT} as an empty file")
endif()

run_compile("${BROKEN}" "${WORK_DIR}/broken.out" broken_result)
if(broken_result EQUAL 0)
    message(FATAL_ERROR "The compile step accepted ${BROKEN} for ${FORMAT}")
endif()
if(EXISTS "${WORK_DIR}/broken.out" OR EXISTS "${WORK_DIR}/broken.out.part")
    message(FATAL_ERROR "The compile step refused ${BROKEN} for ${FORMAT} but left a file that a later build would take "
                        "for a finished shader")
endif()
