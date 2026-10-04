# SPDX-License-Identifier: GPL-3.0-or-later
# Everything that talks to SDL3 or librw, as a library so the tests can use it too (with the headless renderer).
add_library(coney_platform STATIC render_engine.cpp texture_dictionary.cpp texture_viewer_mode.cpp window.cpp)
coney_target_defaults(coney_platform)
target_link_libraries(coney_platform PUBLIC coney_core SDL3::SDL3-static librw::librw)

add_executable(coney main.cpp)
coney_target_defaults(coney)
target_link_libraries(coney PRIVATE coney_platform)

# Headless smoke tests: the NULL renderer needs no display or GPU, so these run on CI and in a future test harness.
add_test(NAME coney.runs_headless COMMAND coney --headless --frames 3)
set_tests_properties(coney.runs_headless PROPERTIES TIMEOUT 30)
add_test(NAME coney.help COMMAND coney --help)
set_tests_properties(coney.help PROPERTIES PASS_REGULAR_EXPRESSION "--view-txd ENTRY")
add_test(NAME coney.rejects_bad_argument COMMAND coney --frames abc)
set_tests_properties(coney.rejects_bad_argument PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.load_needs_disc COMMAND coney --load level1.lev)
set_tests_properties(coney.load_needs_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.view_needs_disc COMMAND coney --view-txd 0x1234)
set_tests_properties(coney.view_needs_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.rejects_missing_disc COMMAND coney --disc "${CMAKE_CURRENT_BINARY_DIR}/no-such-disc" --load x)
set_tests_properties(coney.rejects_missing_disc PROPERTIES WILL_FAIL TRUE)
