// SPDX-License-Identifier: GPL-3.0-or-later
// The screen fade ScreenQueueEffect starts and the menus wait for (docs/research/frontend.md#fades).
#include "graphics/screen_fade.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/screen.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::graphics::ScreenFade;

TEST_CASE("a fade in starts black, waits one frame, then runs to clear over its time", "[screen_fade]") {
    ScreenFade fade;
    CHECK_FALSE(fade.active());
    fade.queue(ScreenFade::kFadeIn, 1.5, 1000);
    CHECK(fade.running());
    CHECK(fade.level() == 1.0F);
    // The first frame only marks it running.
    fade.update(1033);
    CHECK(fade.level() == 1.0F);
    fade.update(1033 + 750);
    CHECK(fade.level() == Approx(0.5F));
    CHECK(fade.active());
    fade.update(1033 + 1500);
    CHECK_FALSE(fade.running());
    CHECK(fade.level() == 0.0F);
    CHECK_FALSE(fade.active());
}

TEST_CASE("a fade out runs 0.2 s shorter than asked, ends black and stays there", "[screen_fade]") {
    ScreenFade fade;
    fade.queue(ScreenFade::kFadeOut, 1.0, 0);
    CHECK(fade.level() == 0.0F);
    CHECK(fade.active());
    fade.update(0);
    CHECK(fade.durationMs() == 800);
    fade.update(400);
    CHECK(fade.level() == Approx(0.5F));
    fade.update(800);
    CHECK_FALSE(fade.running());
    CHECK(fade.level() == 1.0F);
    CHECK(fade.active()); // faded out: the menus still wait
    // 0.2 s or less is not shortened.
    fade.queue(ScreenFade::kFadeIn, 0.0, 1000);
    fade.queue(ScreenFade::kFadeOut, 0.2, 1000);
    fade.update(1000);
    CHECK(fade.durationMs() == 200);
}

TEST_CASE("a zero-length fade jumps; other types do nothing; a new fade replaces a running one", "[screen_fade]") {
    ScreenFade fade;
    fade.queue(ScreenFade::kFadeOut, 0.0, 0);
    CHECK_FALSE(fade.running());
    CHECK(fade.level() == 1.0F);
    fade.queue(7, 1.0, 0);
    CHECK_FALSE(fade.running());
    CHECK(fade.level() == 1.0F);
    fade.queue(ScreenFade::kFadeIn, 1.0, 0);
    fade.update(0);
    fade.update(500);
    fade.queue(ScreenFade::kFadeOut, 0.7, 500);
    CHECK(fade.level() == 0.0F);
    fade.update(500);
    fade.update(750);
    CHECK(fade.level() == Approx(0.5F));
}

TEST_CASE("the fade draws one black quad over the logical screen, and nothing when clear", "[screen_fade]") {
    coney::test::RecordingDevice device;
    ScreenFade fade;
    fade.render(device);
    CHECK(device.draws.empty());
    fade.queue(ScreenFade::kFadeOut, 1.2, 0);
    fade.update(0);
    fade.update(500);
    fade.render(device);
    REQUIRE(device.draws.size() == 1);
    CHECK(device.draws[0].texture == nullptr);
    REQUIRE(device.draws[0].quads.size() == 1);
    const coney::graphics::LogicalQuad& quad = device.draws[0].quads[0];
    CHECK(quad.width == coney::graphics::kLogicalWidth);
    CHECK(quad.height == coney::graphics::kLogicalHeight);
    CHECK(quad.colour.r == 0);
    CHECK(quad.colour.a == 128);
}
