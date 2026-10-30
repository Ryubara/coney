# SPDX-License-Identifier: GPL-3.0-or-later
# The shader compiler Coney's build runs on its HLSL: the `shadercross` command line of SDL_shadercross, built once per
# machine and set of pins as a project of its own, and installed under CONEY_SHADERTOOLS_PREFIX. cmake/coney_shaders.cmake
# configures and builds this project while the main project configures (docs/guides/shaders.md); nobody runs
# it by hand. Why a project of its own: the compiler is a tool of the build, always Release (a Debug LLVM would take
# hours), and the main project's compiler flags, sanitizers and warnings-as-errors must not reach it.
#
# Inputs (all folders, passed by the main project):
#   CONEY_SDL3_SOURCE         SDL3, built here as a minimal static library (the tool needs only its core)
#   CONEY_SHADERCROSS_SOURCE  SDL_shadercross, the command line and the library (two C files)
#   CONEY_SPIRV_CROSS_SOURCE  SPIRV-Cross, SPIR-V to MSL (a library)
#   CONEY_DXC_PREBUILT        Microsoft's DXC release (Windows, Linux x86-64), or empty to build DXC from source
#   CONEY_DXC_SOURCE          the libsdl-org DXC fork with its submodules, when CONEY_DXC_PREBUILT is empty
cmake_minimum_required(VERSION 3.28)
project(coney_shadertools LANGUAGES C CXX)

foreach(_var CONEY_SDL3_SOURCE CONEY_SHADERCROSS_SOURCE CONEY_SPIRV_CROSS_SOURCE)
    if(NOT IS_DIRECTORY "${${_var}}")
        message(FATAL_ERROR "${_var} must name a source folder (set by cmake/coney_shaders.cmake), not '${${_var}}'")
    endif()
endforeach()
if(NOT CONEY_DXC_PREBUILT AND NOT IS_DIRECTORY "${CONEY_DXC_SOURCE}")
    message(FATAL_ERROR "Either CONEY_DXC_PREBUILT or CONEY_DXC_SOURCE must name a folder")
endif()

# SDL3: the core only. shadercross reads and writes files, loads libraries and allocates; it opens no window and no
# device, so every subsystem is off and the build takes a minute.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
foreach(_subsystem AUDIO VIDEO GPU RENDER CAMERA JOYSTICK HAPTIC HIDAPI POWER SENSOR DIALOG TRAY)
    set(SDL_${_subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${CONEY_SDL3_SOURCE}" sdl3 EXCLUDE_FROM_ALL)

# SPIRV-Cross as static libraries, its command line and tests off. The C API (spirv-cross-c) is what shadercross calls.
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory("${CONEY_SPIRV_CROSS_SOURCE}" spirv-cross EXCLUDE_FROM_ALL)

# DXC, HLSL to SPIR-V and to DXIL. Microsoft's release has the compiler only for Windows and Linux x86-64; elsewhere
# (macOS) it is built from the libsdl-org fork, with the settings SDL_shadercross's own vendored build uses.
if(CONEY_DXC_PREBUILT)
    add_library(dxcompiler SHARED IMPORTED)
    if(WIN32)
        # The release holds one folder per architecture; DXC signs DXIL itself since 1.8.2505, so dxil.dll is neither
        # needed nor used (docs/engine/renderer.md#licences).
        string(TOLOWER "${CMAKE_C_COMPILER_ARCHITECTURE_ID}" _dxc_arch)
        if(NOT _dxc_arch MATCHES "^(x64|arm64|x86)$")
            message(FATAL_ERROR "No DXC release for the architecture '${CMAKE_C_COMPILER_ARCHITECTURE_ID}'")
        endif()
        set_target_properties(dxcompiler PROPERTIES
            IMPORTED_LOCATION "${CONEY_DXC_PREBUILT}/bin/${_dxc_arch}/dxcompiler.dll"
            IMPORTED_IMPLIB "${CONEY_DXC_PREBUILT}/lib/${_dxc_arch}/dxcompiler.lib")
    else()
        set_target_properties(dxcompiler PROPERTIES IMPORTED_LOCATION "${CONEY_DXC_PREBUILT}/lib/libdxcompiler.so")
    endif()
else()
    # These are the settings of SDL_shadercross's CMakeLists.txt for its vendored DXC (checked at the pinned commit).
    set(BUILD_SHARED_LIBS OFF)
    # Blocks DXC from adding /D_ITERATOR_DEBUG_LEVEL=0, which only MSVC reads.
    set(HLSL_ENABLE_DEBUG_ITERATORS ON)
    set(DXC_COVERAGE OFF)
    set(HLSL_INCLUDE_TESTS OFF)
    set(LLVM_INCLUDE_TESTS OFF)
    set(HLSL_DISABLE_SOURCE_GENERATION TRUE)
    set(SPIRV_BUILD_TESTS FALSE)
    include("${CONEY_DXC_SOURCE}/cmake/caches/PredefinedParams.cmake")
    add_subdirectory("${CONEY_DXC_SOURCE}" dxc EXCLUDE_FROM_ALL)
endif()

# The command line: SDL_shadercross's two C files, linked statically with SDL3 and SPIRV-Cross (SPIRV-Cross is C++,
# hence the C++ linker). SDL_SHADERCROSS_DXC turns on HLSL compilation through DXC, which it loads as a library.
add_executable(shadercross "${CONEY_SHADERCROSS_SOURCE}/src/cli.c" "${CONEY_SHADERCROSS_SOURCE}/src/SDL_shadercross.c")
target_include_directories(shadercross PRIVATE "${CONEY_SHADERCROSS_SOURCE}/include")
target_compile_definitions(shadercross PRIVATE SDL_SHADERCROSS_DXC)
target_compile_features(shadercross PRIVATE c_std_99)
target_link_libraries(shadercross PRIVATE SDL3::SDL3-static spirv-cross-c dxcompiler)
set_property(TARGET shadercross PROPERTY LINKER_LANGUAGE CXX)
# The compiler library sits beside the executable once installed, and the loader looks there first.
if(APPLE)
    set_property(TARGET shadercross PROPERTY INSTALL_RPATH "@executable_path")
elseif(UNIX)
    set_property(TARGET shadercross PROPERTY INSTALL_RPATH "$ORIGIN")
endif()

install(TARGETS shadercross RUNTIME DESTINATION bin)
install(FILES "$<TARGET_FILE:dxcompiler>" DESTINATION bin)
install(FILES "${CONEY_SHADERCROSS_SOURCE}/LICENSE.txt" DESTINATION licenses RENAME SDL_shadercross.txt)
