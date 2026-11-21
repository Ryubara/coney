# SPDX-License-Identifier: GPL-3.0-or-later
#
# The `playtest` build (CMake's RelWithDebInfo) compiles and links as the release build does, so a bench or a play test
# measures what a player runs (docs/guides/building.md#playtest-flags).
#
# MSVC gives RelWithDebInfo `/O2 /Ob1` (only functions marked inline are inlined) and an incremental link, while
# Release gets `/O2 /Ob2` (any suitable function) and a plain link. The issue 1542 sweep measured the difference on the
# brawl and engaged benches: `/Ob1` left the small vector, ray and capsule helpers as calls through jump thunks, which
# cost the step 19 % (113.6 -> 92.3 ms with 1000 fighters) and, once the pair loops were gone, 40 % (12.1 -> 7.3 ms).
# Gameplay is the same on both (docs/guides/building.md#playtest-flags). GCC and Clang inline at `-O2` whatever the
# build type, so nothing changes for them.
#
# Included once, from the top-level CMakeLists.txt before any target or dependency is made, so the dependencies compile
# with the same flags. Only the RelWithDebInfo variables are touched: Debug, Release and the `ci-release` build keep
# CMake's own. It is safe to include twice.

if(MSVC)
    # /Ob1 -> /Ob2 in the compile flags (CMake writes them as `/O2 /Ob1 /DNDEBUG`).
    foreach(_coney_language C CXX)
        string(REPLACE "/Ob1" "/Ob2" CMAKE_${_coney_language}_FLAGS_RELWITHDEBINFO
               "${CMAKE_${_coney_language}_FLAGS_RELWITHDEBINFO}")
    endforeach()

    # The link: not incremental (an incremental executable pads every function and goes through a jump table, and is
    # twice the size), and with the unreferenced functions and the identical ones folded, which `/debug` alone turns
    # off. The debug information stays (`/debug` is CMake's own). `/INCREMENTAL` and `/INCREMENTAL:NO` are one switch to
    # the linker, so the first is replaced rather than added to.
    foreach(_coney_kind EXE SHARED MODULE)
        string(REPLACE " " ";" _coney_flags "${CMAKE_${_coney_kind}_LINKER_FLAGS_RELWITHDEBINFO}")
        list(FILTER _coney_flags EXCLUDE REGEX "^/(INCREMENTAL(:NO)?|OPT:(REF|ICF))$")
        list(APPEND _coney_flags /INCREMENTAL:NO /OPT:REF /OPT:ICF)
        list(JOIN _coney_flags " " CMAKE_${_coney_kind}_LINKER_FLAGS_RELWITHDEBINFO)
    endforeach()
endif()
