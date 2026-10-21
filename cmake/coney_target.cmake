# SPDX-License-Identifier: GPL-3.0-or-later
option(CONEY_WARNINGS_AS_ERRORS "Fail the build on any compiler warning (CI turns this on)" OFF)
option(CONEY_SANITIZERS "Build Coney's own targets with ASan and UBSan (GCC and Clang only)" OFF)
# The debug tools (docs/guides/building.md#debug-tools): the debug menus, the developer overlay and Dear ImGui, the
# viewers, the sandbox and the developer's command-line options. Off is the release build a player gets: the game alone,
# which does not even fetch Dear ImGui.
option(CONEY_DEBUG_TOOLS "Build the debug tools (off: the release build, the game alone)" ON)

# Settings every Coney target gets. Applied per target, never globally, so dependencies keep their own flags.
function(coney_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        # /fp:precise is the default; stated so the float rules below read the same on every compiler.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /fp:precise)
        if(CONEY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        # No fused multiply-add: GCC contracts across statements by default and Apple clang (arm64) within one, while
        # x86 and MSVC builds do not, so the same float code gave different low bits per platform. The physics kernels
        # follow the original order of sums and lose bits to cancellation, so contraction showed (1e-5).
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
        if(CONEY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(CONEY_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
                                                     -fno-sanitize-recover=all)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()
