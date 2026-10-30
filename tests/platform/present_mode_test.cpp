// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's present mode from `--vsync` and `--fps-cap` (docs/engine/renderer.md#frame-rate): VSYNC,
// MAILBOX for uncapped without tearing, IMMEDIATE. Coney's own: the original presents at the PS2's field rate.
#include "platform/present_mode.h"

#include <catch2/catch_test_macros.hpp>

using coney::platform::choosePresentMode;
using coney::platform::PresentMode;
using coney::platform::PresentModes;

namespace {
constexpr PresentModes kAll{.mailbox = true, .immediate = true};
} // namespace

TEST_CASE("present mode: vsync on waits for the vertical blank at any cap", "[platform][present_mode]") {
    // Expected: ../engine/gpu-renderer.md#present-mode, Coney's own choice
    CHECK(choosePresentMode(true, 0, kAll) == PresentMode::Vsync);
    CHECK(choosePresentMode(true, 144, kAll) == PresentMode::Vsync);
    CHECK(choosePresentMode(true, 0, PresentModes{}) == PresentMode::Vsync);
    CHECK(coney::platform::presentModeName(PresentMode::Vsync) == "vsync");
}

TEST_CASE("present mode: vsync off is mailbox uncapped and immediate under a cap", "[platform][present_mode]") {
    // Expected: ../engine/gpu-renderer.md#present-mode, Coney's own choice
    CHECK(choosePresentMode(false, 0, kAll) == PresentMode::Mailbox);
    CHECK(choosePresentMode(false, 240, kAll) == PresentMode::Immediate);
    CHECK(choosePresentMode(false, 30, kAll) == PresentMode::Immediate);
    CHECK(coney::platform::presentModeName(PresentMode::Mailbox) == "mailbox");
    CHECK(coney::platform::presentModeName(PresentMode::Immediate) == "immediate");
}

TEST_CASE("present mode: a mode the window does not offer falls back to one it does", "[platform][present_mode]") {
    // Expected: ../engine/gpu-renderer.md#present-mode, Coney's own choice
    // No mailbox: uncapped shows each frame at once instead.
    CHECK(choosePresentMode(false, 0, PresentModes{.mailbox = false, .immediate = true}) == PresentMode::Immediate);
    // No immediate: a capped rate takes mailbox, which still does not tear.
    CHECK(choosePresentMode(false, 60, PresentModes{.mailbox = true, .immediate = false}) == PresentMode::Mailbox);
    // Neither: vsync, which every window offers.
    CHECK(choosePresentMode(false, 0, PresentModes{}) == PresentMode::Vsync);
    CHECK(choosePresentMode(false, 60, PresentModes{}) == PresentMode::Vsync);
}
