# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(coney main.cpp window.cpp render_engine.cpp)
coney_target_defaults(coney)
target_link_libraries(coney PRIVATE coney_core SDL3::SDL3-static librw::librw)

# Headless smoke tests: SDL's dummy video driver needs no display, so these run on CI and in a future test harness.
add_test(NAME coney.runs_headless COMMAND coney --frames 3)
set_tests_properties(coney.runs_headless PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)
add_test(NAME coney.help COMMAND coney --help)
set_tests_properties(coney.help PROPERTIES PASS_REGULAR_EXPRESSION "--frames N")
add_test(NAME coney.rejects_bad_argument COMMAND coney --frames abc)
set_tests_properties(coney.rejects_bad_argument PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.load_needs_disc COMMAND coney --load level1.lev)
set_tests_properties(coney.load_needs_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.rejects_missing_disc COMMAND coney --disc "${CMAKE_CURRENT_BINARY_DIR}/no-such-disc" --load x)
set_tests_properties(coney.rejects_missing_disc PROPERTIES WILL_FAIL TRUE)
