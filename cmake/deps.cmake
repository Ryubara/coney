# SPDX-License-Identifier: GPL-3.0-or-later
# Third-party dependencies, pinned to exact commits so every machine and CI builds the same code.
# To update one: change the SHA and the tag comment together, rebuild from a clean build directory, run the tests.
include(FetchContent)

# A folder that keeps the fetched sources between builds, for CI and for every worktree of a machine
# (docs/guides/building.md#ci-caches, #shared-deps): when CONEY_DEPS_SOURCES names one (the environment variable of
# that name gives the default), each dependency is fetched into <folder>/<version>/<name> instead of this build's
# _deps/, and once that holds a complete fetch of these pins (its stamp holds their SHA-256) it is used as it is, with
# no download. The path is the same on every run and for every preset, so the compiler cache sees the same command
# lines. Empty (the default): nothing changes.
set(CONEY_DEPS_SOURCES "$ENV{CONEY_DEPS_SOURCES}" CACHE PATH "Folder that keeps the fetched dependency sources")
# The version of the sources: the pin lines alone (repositories, tags, URLs, patches), so a comment or a build option
# changed here neither re-fetches nor moves the folder. Each dependency's stamp holds it.
file(STRINGS "${CMAKE_CURRENT_LIST_FILE}" _coney_pins REGEX "^[^#]*(GIT_REPOSITORY|GIT_TAG|URL|PATCH_COMMAND)")
string(SHA256 _coney_deps_hash "${_coney_pins}")
# Dear ImGui only with the debug tools (docs/guides/building.md#debug-tools): the release build fetches and builds none.
set(_coney_deps SDL3 librw Catch2 enet ffmpeg)
if(CONEY_DEBUG_TOOLS)
    list(APPEND _coney_deps imgui)
endif()
# The sources of this version live in <CONEY_DEPS_SOURCES>/<first 16 hex digits of the version>, worked out on every
# configure, never kept in the cache: a folder configured before a pin changed moves to the new version's folder at
# its next configure instead of fetching the new pins into the old one under other worktrees' builds. Old versions'
# folders are never removed by the build (docs/guides/building.md#shared-deps).
set(_coney_deps_dir "")
if(CONEY_DEPS_SOURCES)
    string(SUBSTRING "${_coney_deps_hash}" 0 16 _coney_deps_version)
    file(TO_CMAKE_PATH "${CONEY_DEPS_SOURCES}/${_coney_deps_version}" _coney_deps_dir)
endif()
# Several configures may share the folder: `coney-tools build` points every worktree of a machine at it
# (docs/guides/building.md#shared-deps). So while any dependency in it lacks this file's stamp,
# take the folder's lock before looking again: one configure fetches, the others wait for it and then find its
# stamps. The lock is held until this file is done (GUARD FILE) and ends with the process if it dies.
if(CONEY_DEPS_SOURCES)
    set(_coney_deps_complete TRUE)
    foreach(_dep IN LISTS _coney_deps)
        set(_stamp "")
        if(EXISTS "${_coney_deps_dir}/${_dep}/.coney-deps-hash")
            file(READ "${_coney_deps_dir}/${_dep}/.coney-deps-hash" _stamp)
        endif()
        if(NOT _stamp STREQUAL _coney_deps_hash)
            set(_coney_deps_complete FALSE)
        endif()
    endforeach()
    if(NOT _coney_deps_complete)
        file(MAKE_DIRECTORY "${_coney_deps_dir}")
        file(LOCK "${_coney_deps_dir}" DIRECTORY GUARD FILE TIMEOUT 1800)
    endif()
endif()
foreach(_dep IN LISTS _coney_deps)
    # _coney_<name>_where: the SOURCE_DIR option for its declaration when it is fetched into the folder, else empty.
    set(_coney_${_dep}_where "")
    if(CONEY_DEPS_SOURCES)
        file(TO_CMAKE_PATH "${_coney_deps_dir}/${_dep}" _dir)
        string(TOUPPER "${_dep}" _upper)
        set(_stamp "")
        if(EXISTS "${_dir}/.coney-deps-hash")
            file(READ "${_dir}/.coney-deps-hash" _stamp)
        endif()
        # FetchContent writes the folder it used into the cache as FETCHCONTENT_SOURCE_DIR_<NAME> (empty when it fetched
        # into the folder given in the declaration), and an entry there counts as the user's own copy. So the folder
        # this file chose is remembered in CONEY_DEPS_PICKED_<NAME>, and an empty entry, or one that still holds it, is
        # ours from an earlier configure, possibly of other pins: it is dropped and chosen again below. A build folder
        # from before that memory has none, and its entry is ours when it has the shape of one,
        # <CONEY_DEPS_SOURCES>/<version>/<name>; the price is that a copy the user names inside the shared folder is
        # replaced in such a folder. An entry holding anything else is the user's (-D...), and wins.
        if(DEFINED FETCHCONTENT_SOURCE_DIR_${_upper})
            set(_ours FALSE)
            if("${FETCHCONTENT_SOURCE_DIR_${_upper}}" STREQUAL "")
                set(_ours TRUE)
            elseif(DEFINED CACHE{CONEY_DEPS_PICKED_${_upper}})
                if("${FETCHCONTENT_SOURCE_DIR_${_upper}}" STREQUAL "$CACHE{CONEY_DEPS_PICKED_${_upper}}")
                    set(_ours TRUE)
                endif()
            else()
                cmake_path(GET FETCHCONTENT_SOURCE_DIR_${_upper} FILENAME _entry_name)
                cmake_path(GET FETCHCONTENT_SOURCE_DIR_${_upper} PARENT_PATH _entry_version_dir)
                cmake_path(GET _entry_version_dir FILENAME _entry_version)
                cmake_path(GET _entry_version_dir PARENT_PATH _entry_base)
                file(TO_CMAKE_PATH "${CONEY_DEPS_SOURCES}" _base)
                if(_entry_name STREQUAL _dep AND _entry_version MATCHES "^[0-9a-f]+$" AND _entry_base STREQUAL _base)
                    set(_ours TRUE)
                endif()
            endif()
            if(_ours)
                unset(FETCHCONTENT_SOURCE_DIR_${_upper} CACHE)
                unset(FETCHCONTENT_SOURCE_DIR_${_upper})
            endif()
        endif()
        unset(CONEY_DEPS_PICKED_${_upper} CACHE)
        if(DEFINED FETCHCONTENT_SOURCE_DIR_${_upper})
            # Someone pointed it at their own copy; leave that alone.
        elseif(_stamp STREQUAL _coney_deps_hash)
            set(FETCHCONTENT_SOURCE_DIR_${_upper} "${_dir}")
            set(CONEY_DEPS_PICKED_${_upper} "${_dir}" CACHE INTERNAL "The folder deps.cmake chose for ${_dep}")
        else()
            set(_coney_${_dep}_where SOURCE_DIR "${_dir}")
        endif()
    endif()
endforeach()

# SYSTEM: their headers are treated as system headers, so their warnings never fail our -Werror build.

# SDL3 release-3.4.18, zlib licence. Static, so the executable needs no DLL next to it.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
# Audio on (SDL's default, stated because Coney's sound output needs it): src/platform/sdl_audio_device.cpp.
set(SDL_AUDIO ON CACHE BOOL "" FORCE)
# OVERRIDE_FIND_PACKAGE: librw's GL3 backend looks SDL3 up with find_package(SDL3 CONFIG REQUIRED) and links
# SDL3::SDL3. With this option CMake answers that find_package with this fetched copy (it writes a small
# sdl3-config.cmake into CMAKE_FIND_PACKAGE_REDIRECTS_DIR), so librw links the same static SDL3 as Coney (SDL3::SDL3
# is SDL's own alias of SDL3-static when only the static library is built) and an SDL3 installed on the machine is
# never picked up. It needs CMake 3.24; we require 3.28.
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                          GIT_TAG 829a65d769d935c4852f8159e964312c0957260a ${_coney_SDL3_where}
                          SYSTEM OVERRIDE_FIND_PACKAGE)

# librw master at 2026-08-26, MIT licence. Built for its GL3 platform with SDL3 as the window and context library
# (LIBRW_GL3_GFXLIB=SDL3), which needs the OpenGL headers and libraries: libgl1-mesa-dev on Linux, the system's own on
# Windows and macOS. A librw build has exactly one platform, fixed at compile time; Coney's headless mode installs
# librw's NULL device at run time instead (src/platform/render_engine.cpp), so no second build is needed.
# The library target is librw::librw (an alias of librw).
set(LIBRW_PLATFORM "GL3" CACHE STRING "" FORCE)
set(LIBRW_GL3_GFXLIB "SDL3" CACHE STRING "" FORCE)
set(LIBRW_TOOLS OFF CACHE BOOL "" FORCE)
set(LIBRW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(librw GIT_REPOSITORY https://github.com/aap/librw.git
                           GIT_TAG 18532c2e13efbc1aa43f0b00b4459831f9414de0 ${_coney_librw_where} SYSTEM)

# Catch2 v3.16.0, Boost Software Licence. Tests only.
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git
                            GIT_TAG 317ac1ed4c0bb6e6b91eafc817e05c488feffcb3 ${_coney_Catch2_where} SYSTEM)

# Dear ImGui v1.92.9b-docking (an annotated tag: the commit below is the one it points at, not the tag object's), MIT
# licence: v1.92.9b with the docking branch merged in. The developer overlay of the debug menus
# (docs/guides/debug-menu.md), used only in src/platform/. It ships no CMake build, so the library is made below from
# its core, its std::string helpers and the SDL3 and OpenGL 3 backends.
if(CONEY_DEBUG_TOOLS)
    FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git
                               GIT_TAG b48d1afbe8ee8b238e2961dc363a949dd7304e23 ${_coney_imgui_where} SYSTEM)
endif()

# ENet v1.3.18 (a lightweight tag, so the commit is the tag's own), MIT licence: the UDP transport of network play
# (docs/guides/multiplayer.md#enet), used only in src/platform/. Its own CMakeLists.txt would add a target named enet
# with directory-wide flags, so SOURCE_SUBDIR points at a folder it does not have: FetchContent then only fetches, and
# the library is made below from its C files, as Dear ImGui's is.
FetchContent_Declare(enet GIT_REPOSITORY https://github.com/lsalzman/enet.git
                          GIT_TAG 2662c0de09e36f2a2030ccc2c528a3e4c9e8138a ${_coney_enet_where}
                          SOURCE_SUBDIR coney-no-cmake SYSTEM)

FetchContent_MakeAvailable(SDL3 librw Catch2 enet)

# Dear ImGui as a static library, with the debug tools only. Its OpenGL 3 backend loads the GL functions itself (its
# bundled loader), so it needs no GL import library; on Linux and macOS the loader opens the GL library with dlopen.
if(CONEY_DEBUG_TOOLS)
    FetchContent_MakeAvailable(imgui)
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
                                                   ${imgui_SOURCE_DIR}/misc/cpp)
    # No ImGui API that a later version removes, so an update fails loudly instead of compiling against stale names.
    target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_compile_features(imgui PUBLIC cxx_std_23)
    target_link_libraries(imgui PUBLIC SDL3::SDL3-static ${CMAKE_DL_LIBS})
endif()

# ENet as a static library. Its configure step only detects socket functions that every system Coney supports has:
# macOS gets them from unix.c's own defaults, Linux from the definitions here; Windows needs Winsock and the
# multimedia timer library it times itself with.
add_library(enet STATIC
    ${enet_SOURCE_DIR}/callbacks.c ${enet_SOURCE_DIR}/compress.c ${enet_SOURCE_DIR}/host.c ${enet_SOURCE_DIR}/list.c
    ${enet_SOURCE_DIR}/packet.c ${enet_SOURCE_DIR}/peer.c ${enet_SOURCE_DIR}/protocol.c ${enet_SOURCE_DIR}/unix.c
    ${enet_SOURCE_DIR}/win32.c)
target_include_directories(enet SYSTEM PUBLIC ${enet_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(enet PUBLIC ws2_32 winmm)
elseif(NOT APPLE)
    target_compile_definitions(enet PRIVATE HAS_FCNTL=1 HAS_POLL=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1 HAS_INET_PTON=1
                                            HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1 HAS_SOCKLEN_T=1)
endif()

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

# librw's own asserts off in every build type. Its PS2 texture reader recomputes the GS register layout of each
# texture and asserts that the stream agrees; some of the game's textures (palette placement, `paletteBase`) do not,
# and in a Debug build that aborted the whole program, where a release build simply keeps the stream's values, which
# is what the game's data needs. Coney checks the streams it hands librw itself (src/graphics/rw_stream.h).
target_compile_definitions(librw PRIVATE NDEBUG)

# Catch2's CMake helpers (catch_discover_tests) live in its extras folder, which FetchContent does not put on the
# module path by itself; tests/CMakeLists.txt needs it for include(Catch).
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")

# FFmpeg 9.0.2, LGPL-2.1-or-later: the Bink demuxer and the Bink video and Bink audio (DCT) decoders for the game's
# movies (docs/research/movies.md), used only in src/platform/. The release tarball, checked against its SHA-256; it
# has no CMake build of its own, so cmake/ffmpeg/CMakeLists.txt compiles the trimmed library from it (why, and how to
# swap in another FFmpeg: docs/guides/building.md#ffmpeg). The library target is ffmpeg_bink.
set(FFMPEG_VERSION "9.0.2")
FetchContent_Declare(ffmpeg URL https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz
                            URL_HASH SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
                            DOWNLOAD_EXTRACT_TIMESTAMP ON ${_coney_ffmpeg_where})
FetchContent_MakeAvailable(ffmpeg)
set(FFMPEG_SOURCE_DIR "${ffmpeg_SOURCE_DIR}")

# Mark each dependency fetched into CONEY_DEPS_SOURCES as complete, only now that every fetch above has succeeded, so
# a fetch that failed half-way is never taken for a finished one.
foreach(_dep IN LISTS _coney_deps)
    if(_coney_${_dep}_where)
        list(GET _coney_${_dep}_where 1 _dir)
        file(WRITE "${_dir}/.coney-deps-hash" "${_coney_deps_hash}")
    endif()
endforeach()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ffmpeg" "${CMAKE_BINARY_DIR}/_deps/ffmpeg-coney")
