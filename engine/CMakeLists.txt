cmake_minimum_required(VERSION 3.20)
project(coney_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CONEY_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

# The engine library: formats, core (settings, logging, clock, frame pacer, main loop). It does not use SDL.
add_library(coney_engine STATIC
    src/core/command_line.cpp
    src/core/frame_pacer.cpp
    src/core/log.cpp
    src/core/main_loop.cpp
    src/formats/chunk_container.cpp
)
target_include_directories(coney_engine PUBLIC src)
target_compile_options(coney_engine PRIVATE ${CONEY_WARNINGS})

# The platform layer: the only code that includes SDL headers.
find_package(SDL3 REQUIRED CONFIG)
add_library(coney_platform STATIC
    src/platform/gpu.cpp
    src/platform/platform.cpp
    src/platform/window.cpp
)
target_link_libraries(coney_platform PUBLIC coney_engine PRIVATE SDL3::SDL3)
target_compile_options(coney_platform PRIVATE ${CONEY_WARNINGS})

add_executable(coney app/main.cpp)
target_link_libraries(coney PRIVATE coney_platform)
target_compile_options(coney PRIVATE ${CONEY_WARNINGS})

find_package(Catch2 3 REQUIRED)
include(Catch)
enable_testing()

add_executable(coney_tests
    tests/chunk_container_tests.cpp
    tests/command_line_tests.cpp
    tests/frame_pacer_tests.cpp
    tests/log_tests.cpp
    tests/main_loop_tests.cpp
)
target_link_libraries(coney_tests PRIVATE coney_engine Catch2::Catch2WithMain)
target_compile_options(coney_tests PRIVATE ${CONEY_WARNINGS})
catch_discover_tests(coney_tests)

# Tests that open a window run on SDL's offscreen video driver, set here rather than in the code.
set(CONEY_HEADLESS_ENV "SDL_VIDEODRIVER=offscreen")

add_executable(coney_platform_tests tests/platform_tests.cpp)
target_link_libraries(coney_platform_tests PRIVATE coney_platform SDL3::SDL3 Catch2::Catch2WithMain)
target_compile_options(coney_platform_tests PRIVATE ${CONEY_WARNINGS})
catch_discover_tests(coney_platform_tests PROPERTIES ENVIRONMENT "${CONEY_HEADLESS_ENV}")

add_test(NAME coney_runs_frames COMMAND coney --frames 5 --log-level debug)
set_tests_properties(coney_runs_frames PROPERTIES ENVIRONMENT "${CONEY_HEADLESS_ENV}")
add_test(NAME coney_rejects_bad_options COMMAND coney --frames zero)
set_tests_properties(coney_rejects_bad_options PROPERTIES WILL_FAIL TRUE)

# SDL stays behind the platform layer: no other engine source may include an SDL header.
add_test(NAME engine_sdl_confined
    COMMAND ${CMAKE_COMMAND} -DENGINE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_sdl_confined.cmake)
