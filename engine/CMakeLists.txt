cmake_minimum_required(VERSION 3.20)
project(coney_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(coney_engine STATIC src/formats/chunk_container.cpp)
target_include_directories(coney_engine PUBLIC src)
target_compile_options(coney_engine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

find_package(Catch2 3 REQUIRED)

enable_testing()
add_executable(coney_tests tests/chunk_container_tests.cpp)
target_link_libraries(coney_tests PRIVATE coney_engine Catch2::Catch2WithMain)
target_compile_options(coney_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

include(Catch)
catch_discover_tests(coney_tests)
