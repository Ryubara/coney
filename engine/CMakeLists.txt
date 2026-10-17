cmake_minimum_required(VERSION 3.20)
project(coney_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR "The engine builds with GCC or Clang, not ${CMAKE_CXX_COMPILER_ID}")
endif()

# AUTO builds the sanitized suite when the toolchain can link it, ON requires it, OFF leaves it out.
set(CONEY_SANITIZED_TESTS AUTO CACHE STRING "Also build and run the tests under AddressSanitizer and UBSan")
set_property(CACHE CONEY_SANITIZED_TESTS PROPERTY STRINGS AUTO ON OFF)

# The engine's sources and the tests' sources. New files are added to these lists.
# The engine library: formats, core (settings, logging, clock, frame pacer, main loop), game behaviour and input
# mapping. It does not use SDL.
set(CONEY_ENGINE_SOURCES
    src/core/command_line.cpp
    src/core/frame_pacer.cpp
    src/core/log.cpp
    src/core/main_loop.cpp
    src/formats/chunk_container.cpp
    src/game/pad.cpp
    src/input/bindings.cpp
    src/input/input_system.cpp
)
set(CONEY_TEST_SOURCES
    tests/bindings_tests.cpp
    tests/chunk_container_fuzz_tests.cpp
    tests/chunk_container_tests.cpp
    tests/command_line_tests.cpp
    tests/frame_pacer_tests.cpp
    tests/input_system_tests.cpp
    tests/log_tests.cpp
    tests/main_loop_tests.cpp
    tests/pad_commands_tests.cpp
    tests/pad_sticks_tests.cpp
)
# The platform layer: the only code that includes SDL headers.
set(CONEY_PLATFORM_SOURCES
    src/platform/gpu.cpp
    src/platform/platform.cpp
    src/platform/window.cpp
)
set(CONEY_PLATFORM_TEST_SOURCES
    tests/gamepad_hotplug_tests.cpp
    tests/platform_tests.cpp
)

set(CONEY_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
set(CONEY_SANITIZERS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)

find_package(Catch2 3 REQUIRED)
include(CTest)
include(Catch)

# One build of the engine library and of the suite that tests it. `flags` go to both the compiler and the linker.
function(coney_add_variant suffix test_prefix)
    set(flags ${ARGN})
    add_library(coney_engine${suffix} STATIC ${CONEY_ENGINE_SOURCES})
    target_include_directories(coney_engine${suffix} PUBLIC src)
    target_compile_options(coney_engine${suffix} PRIVATE ${CONEY_WARNINGS} PUBLIC ${flags})
    target_link_options(coney_engine${suffix} PUBLIC ${flags})

    if(BUILD_TESTING)
        add_executable(coney_tests${suffix} ${CONEY_TEST_SOURCES})
        target_link_libraries(coney_tests${suffix} PRIVATE coney_engine${suffix} Catch2::Catch2WithMain)
        target_compile_options(coney_tests${suffix} PRIVATE ${CONEY_WARNINGS})
        catch_discover_tests(coney_tests${suffix} TEST_PREFIX "${test_prefix}")
    endif()
endfunction()

coney_add_variant("" "")

if(BUILD_TESTING AND NOT CONEY_SANITIZED_TESTS STREQUAL "OFF")
    include(CheckCXXSourceCompiles)
    string(JOIN " " CMAKE_REQUIRED_FLAGS ${CONEY_SANITIZERS})
    set(CMAKE_REQUIRED_LINK_OPTIONS ${CONEY_SANITIZERS})
    check_cxx_source_compiles("int main() { return 0; }" CONEY_HAVE_SANITIZERS)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(CONEY_HAVE_SANITIZERS)
        coney_add_variant(_sanitized "sanitized/" ${CONEY_SANITIZERS})
    elseif(CONEY_SANITIZED_TESTS STREQUAL "ON")
        message(FATAL_ERROR "CONEY_SANITIZED_TESTS is ON but ${CMAKE_CXX_COMPILER} cannot link the sanitizers")
    else()
        message(WARNING "${CMAKE_CXX_COMPILER} cannot link the sanitizers (is its runtime installed?); "
                        "the sanitized test suite is not built")
    endif()
endif()

find_package(SDL3 REQUIRED CONFIG)
add_library(coney_platform STATIC ${CONEY_PLATFORM_SOURCES})
target_link_libraries(coney_platform PUBLIC coney_engine PRIVATE SDL3::SDL3)
target_compile_options(coney_platform PRIVATE ${CONEY_WARNINGS})

add_executable(coney app/main.cpp)
target_link_libraries(coney PRIVATE coney_platform)
target_compile_options(coney PRIVATE ${CONEY_WARNINGS})

if(BUILD_TESTING)
    # Tests that open a window run on SDL's offscreen video driver, set here rather than in the code.
    set(CONEY_HEADLESS_ENV "SDL_VIDEODRIVER=offscreen")

    add_executable(coney_platform_tests ${CONEY_PLATFORM_TEST_SOURCES})
    target_link_libraries(coney_platform_tests PRIVATE coney_platform SDL3::SDL3 Catch2::Catch2WithMain)
    target_compile_options(coney_platform_tests PRIVATE ${CONEY_WARNINGS})
    catch_discover_tests(coney_platform_tests PROPERTIES ENVIRONMENT "${CONEY_HEADLESS_ENV}")

    add_test(NAME coney_runs_frames COMMAND coney --frames 5 --log-level debug)
    set_tests_properties(coney_runs_frames PROPERTIES ENVIRONMENT "${CONEY_HEADLESS_ENV}")
    add_test(NAME coney_rejects_bad_options COMMAND coney --frames zero)
    set_tests_properties(coney_rejects_bad_options PROPERTIES WILL_FAIL TRUE)

    # SDL stays behind the platform layer: no other engine source may include an SDL header.
    add_test(NAME engine_sdl_confined
        COMMAND ${CMAKE_COMMAND} -DENGINE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_sdl_confined.cmake)

    # The bindings in use, written as a settings file.
    add_test(NAME coney_writes_bindings COMMAND coney --write-bindings ${CMAKE_CURRENT_BINARY_DIR}/bindings.ini)
endif()
