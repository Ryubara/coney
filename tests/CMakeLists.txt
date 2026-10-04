# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(coney_tests core/options_test.cpp)
coney_target_defaults(coney_tests)
target_link_libraries(coney_tests PRIVATE coney_core Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(coney_tests)
