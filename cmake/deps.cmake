# SPDX-License-Identifier: GPL-3.0-or-later
# Third-party dependencies, pinned to exact commits so every machine and CI builds the same code.
# To update one: change the SHA and the tag comment together, rebuild from a clean build directory, run the tests.
include(FetchContent)

# SYSTEM: their headers are treated as system headers, so their warnings never fail our -Werror build.

# SDL3 release-3.4.18, zlib licence. Static, so the executable needs no DLL next to it.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                          GIT_TAG 829a65d769d935c4852f8159e964312c0957260a SYSTEM)

# librw master at 2026-08-26, MIT licence. The NULL platform: librw's GL3 backend can draw through SDL3
# (LIBRW_GL3_GFXLIB=SDL3), but it looks SDL3 up with find_package(SDL3 CONFIG), which the SDL3 fetched above does not
# satisfy without extra wiring, and it needs OpenGL. Wiring that up belongs with the renderer, so for now Coney
# starts librw without a renderer and owns the window itself (see render_engine.h).
# The library target is librw::librw (an alias of librw).
set(LIBRW_PLATFORM "NULL" CACHE STRING "" FORCE)
set(LIBRW_TOOLS OFF CACHE BOOL "" FORCE)
set(LIBRW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(librw GIT_REPOSITORY https://github.com/aap/librw.git
                           GIT_TAG 18532c2e13efbc1aa43f0b00b4459831f9414de0 SYSTEM)

# Catch2 v3.16.0, Boost Software Licence. Tests only.
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git
                            GIT_TAG 317ac1ed4c0bb6e6b91eafc817e05c488feffcb3 SYSTEM)

FetchContent_MakeAvailable(SDL3 librw Catch2)

# Catch2 builds as C++14 by default, which leaves out its std::string_view support (C++17 and later) from the
# compiled library while our C++23 tests still see it declared, and the tests then fail to link. Build it with the
# same standard as our code, so the header and the library agree.
target_compile_features(Catch2 PUBLIC cxx_std_23)

# On MSVC librw adds /wd4996 /wd4244 as PUBLIC options, which would switch off the deprecation (C4996) and narrowing
# (C4244) warnings in every Coney target that links librw::librw. Adding /w44244 on our side does not help: librw's
# flag comes later on the command line and wins. So drop librw's interface options; its own sources still compile
# with them, because COMPILE_OPTIONS (what librw itself uses) is a separate property from INTERFACE_COMPILE_OPTIONS.
if(MSVC)
    set_property(TARGET librw PROPERTY INTERFACE_COMPILE_OPTIONS "")
endif()

# Catch2's CMake helpers (catch_discover_tests) live in its extras folder, which FetchContent does not put on the
# module path by itself; tests/CMakeLists.txt needs it for include(Catch).
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
