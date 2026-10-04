// SPDX-License-Identifier: GPL-3.0-or-later
// The screen fade ScreenQueueEffect starts and the menus wait for (docs/research/frontend.md#profile-manager).
#include "graphics/screen_fade.h"

#include <catch2/catch_test_macros.hpp>

#include "graphics/screen.h"
#include "support/recording_device.h"

using coney::graphics::ScreenFade;

TEST_CASE("a fade in runs from black to clear over its time, then stops", "[screen_fade]") {
    ScreenFade fade;
    CHECK_FALSE(fade.active());
    fade.queue(ScreenFade::kFadeIn, 1.5, 1000);
    CHECK(fade.running());
    CHECK(fade.level() == 1.0F);
    fade.update(1750);
    CHECK(fade.level() == 0.5F);
    CHECK(fade.active());
    fade.update(2500);
    CHECK_FALSE(fade.running());
    CHECK(fade.level() == 0.0F);
    CHECK_FALSE(fade.active());
}

TEST_CASE("a fade out ends black and stays there; a zero-length fade jumps; other types do nothing", "[screen_fade]") {
    ScreenFade fade;
    fade.queue(ScreenFade::kFadeOut, 0.7, 0);
    fade.update(350);
    CHECK(fade.level() == 0.5F);
    fade.update(10'000);
    CHECK_FALSE(fade.running());
    CHECK(fade.level() == 1.0F);
    CHECK(fade.active()); // faded out: the menus still wait
    fade.queue(ScreenFade::kFadeIn, 0.0, 10'000);
    CHECK_FALSE(fade.running());
    CHECK(fade.level() == 0.0F);
    fade.queue(7, 1.0, 10'000);
    CHECK_FALSE(fade.running());
}

TEST_CASE("the fade draws one black quad over the logical screen, and nothing when clear", "[screen_fade]") {
    coney::test::RecordingDevice device;
    ScreenFade fade;
    fade.render(device);
    CHECK(device.draws.empty());
    fade.queue(ScreenFade::kFadeOut, 1.0, 0);
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
