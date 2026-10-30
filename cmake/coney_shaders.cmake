# SPDX-License-Identifier: GPL-3.0-or-later
# The HLSL shader build (docs/guides/shaders.md): the shader compiler, built once per machine and set of pins
# while the project configures, and coney_add_shader_library(), which compiles a folder of HLSL to SPIR-V (Vulkan),
# DXIL (Direct3D 12) and MSL (Metal) at build time and embeds the results in a library.
include_guard(GLOBAL)

# The compiler is SDL_shadercross's command line (cmake/shadertools/CMakeLists.txt), built from the sources
# cmake/deps.cmake fetched and installed under the prefix chosen here. With the shared dependency folder
# (CONEY_DEPS_SOURCES) that is beside the sources, so every build folder of the machine, and CI's dependency cache,
# builds it once; else it is inside this build folder. Sets CONEY_SHADERCROSS to the installed executable. A stamp holds
# the pins and the tool project's own hash, so a changed pin or project builds it again; a lock keeps two configures
# from building it at once.
function(coney_provision_shader_compiler)
    if(CONEY_DEPS_SOURCES)
        set(_prefix "${_coney_deps_dir}/shadertools")
    else()
        set(_prefix "${CMAKE_BINARY_DIR}/_shadertools")
    endif()
    set(_tool "${_prefix}/bin/shadercross${CMAKE_EXECUTABLE_SUFFIX}")
    set(_project "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/shadertools")
    file(SHA256 "${_project}/CMakeLists.txt" _project_hash)
    string(SHA256 _hash "${_coney_deps_hash} ${_project_hash}")

    file(MAKE_DIRECTORY "${_prefix}")
    file(LOCK "${_prefix}/.lock" TIMEOUT 7200 RESULT_VARIABLE _locked)
    if(NOT _locked EQUAL 0)
        message(FATAL_ERROR "Waited two hours for another configure to build the shader compiler (${_locked})")
    endif()
    set(_stamp "")
    if(EXISTS "${_prefix}/.coney-deps-hash")
        file(READ "${_prefix}/.coney-deps-hash" _stamp)
    endif()
    if(NOT _stamp STREQUAL _hash OR NOT EXISTS "${_tool}")
        message(STATUS "Building the shader compiler (SDL_shadercross, DXC): once for these pins, then kept in ${_prefix}")
        file(REMOVE "${_prefix}/.coney-deps-hash")
        set(_build "${CMAKE_BINARY_DIR}/_shadertools-build")
        set(_arguments
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DCMAKE_INSTALL_PREFIX=${_prefix}
            -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
            -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
            -DCONEY_SDL3_SOURCE=${SDL3_SOURCE_DIR}
            -DCONEY_SHADERCROSS_SOURCE=${shadercross_SOURCE_DIR})
        # The compiler cache, when the main build has one.
        foreach(_language C CXX)
            if(CMAKE_${_language}_COMPILER_LAUNCHER)
                list(APPEND _arguments -DCMAKE_${_language}_COMPILER_LAUNCHER=${CMAKE_${_language}_COMPILER_LAUNCHER})
            endif()
        endforeach()
        if(_coney_dxc_from_source)
            list(APPEND _arguments
                -DCONEY_SPIRV_CROSS_SOURCE=${shadercross_SOURCE_DIR}/external/SPIRV-Cross
                -DCONEY_DXC_SOURCE=${shadercross_SOURCE_DIR}/external/DirectXShaderCompiler)
        else()
            list(APPEND _arguments
                -DCONEY_SPIRV_CROSS_SOURCE=${spirv_cross_SOURCE_DIR}
                -DCONEY_DXC_PREBUILT=${dxc_SOURCE_DIR})
        endif()
        foreach(_step IN ITEMS configure build install)
            if(_step STREQUAL "configure")
                set(_command "${CMAKE_COMMAND}" -S "${_project}" -B "${_build}" -G "${CMAKE_GENERATOR}" ${_arguments})
            elseif(_step STREQUAL "build")
                set(_command "${CMAKE_COMMAND}" --build "${_build}" --target shadercross)
            else()
                set(_command "${CMAKE_COMMAND}" --install "${_build}")
            endif()
            execute_process(COMMAND ${_command} RESULT_VARIABLE _result)
            if(NOT _result EQUAL 0)
                message(FATAL_ERROR "The shader compiler's ${_step} failed (${_result}); its log is above. "
                                    "Configure with -DCONEY_SHADERS=OFF to build Coney without the shaders.")
            endif()
        endforeach()
        file(WRITE "${_prefix}/.coney-deps-hash" "${_hash}")
    endif()
    file(LOCK "${_prefix}/.lock" RELEASE)
    set(CONEY_SHADERCROSS "${_tool}" CACHE INTERNAL "The shader compiler (docs/guides/shaders.md)")
endfunction()

# coney_add_shader_library(<target> DIRECTORY <folder>)
#
# Makes the static library <target> that embeds every HLSL file of <folder> (src/platform/shaders/embedded_shaders.h
# is its interface). A file is named <name>.<stage>.hlsl with the stage vert, frag or comp; its entry point is `main`.
# For each file the build compiles SPIR-V, DXIL and MSL and reflects the resource counts, each in a step of its own
# that fails the build when the shader does not compile for that format. The folder's list is read at every configure
# and again when a file is added or removed (CONFIGURE_DEPENDS).
function(coney_add_shader_library target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "DIRECTORY" "")
    file(GLOB _sources CONFIGURE_DEPENDS "${ARG_DIRECTORY}/*.hlsl")
    list(SORT _sources)
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/compiled")
    set(_outputs "")
    set(_shaders "")
    foreach(_source IN LISTS _sources)
        get_filename_component(_name "${_source}" NAME_WLE)
        if(_name MATCHES "\\.vert$")
            set(_stage vertex)
        elseif(_name MATCHES "\\.frag$")
            set(_stage fragment)
        elseif(_name MATCHES "\\.comp$")
            set(_stage compute)
        else()
            message(FATAL_ERROR "${_source}: a shader file is named <name>.vert.hlsl, <name>.frag.hlsl or <name>.comp.hlsl")
        endif()
        list(APPEND _shaders "${_name}:${_stage}")
        foreach(_pair IN ITEMS SPIRV:spv DXIL:dxil MSL:msl JSON:json)
            string(REPLACE ":" ";" _pair "${_pair}")
            list(GET _pair 0 _format)
            list(GET _pair 1 _extension)
            set(_file "${_out}/${_name}.${_extension}")
            add_custom_command(
                OUTPUT "${_file}"
                COMMAND ${CMAKE_COMMAND} -DTOOL=${CONEY_SHADERCROSS} -DSOURCE=${_source} -DOUTPUT=${_file}
                        -DFORMAT=${_format} -DSTAGE=${_stage} -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/coney_shader_compile.cmake"
                DEPENDS "${_source}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/coney_shader_compile.cmake"
                COMMENT "Compiling ${_name} to ${_format}"
                VERBATIM)
            list(APPEND _outputs "${_file}")
        endforeach()
    endforeach()

    # The list reaches the script joined by commas: a semicolon in a command line is a shell's command separator.
    list(JOIN _shaders "," _shaders_argument)
    set(_data "${CMAKE_CURRENT_BINARY_DIR}/embedded_shader_data.inc")
    add_custom_command(
        OUTPUT "${_data}"
        COMMAND ${CMAKE_COMMAND} "-DOUTPUT=${_data}" "-DFOLDER=${_out}" "-DSHADERS=${_shaders_argument}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/coney_shader_embed.cmake"
        DEPENDS ${_outputs} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/coney_shader_embed.cmake"
        COMMENT "Embedding the compiled shaders"
        VERBATIM)

    add_library(${target} STATIC "${CMAKE_CURRENT_SOURCE_DIR}/embedded_shaders.cpp" "${_data}")
    coney_target_defaults(${target})
    target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    # The data file is included, not compiled; listing it as a source makes the build produce it first.
    set_source_files_properties("${_data}" PROPERTIES HEADER_FILE_ONLY TRUE)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/embedded_shaders.cpp" APPEND PROPERTY OBJECT_DEPENDS "${_data}")
endfunction()
