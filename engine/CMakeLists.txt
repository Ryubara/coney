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
set(CONEY_ENGINE_SOURCES
    src/formats/chunk_container.cpp
)
set(CONEY_TEST_SOURCES
    tests/chunk_container_tests.cpp
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
