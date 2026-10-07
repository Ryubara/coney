// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/mug_meter.h"

#include <cstdint>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/recording_device.h"

using Catch::Approx;
using coney::hud::MugMeter;
using coney::hud::MugMeterMode;

namespace {

// A sheet of `count` equal rectangles over a small fake texture.
coney::graphics::SpriteSheet fakeSheet(int count) {
    coney::graphics::SpriteSheet sheet;
    sheet.texture = std::make_shared<coney::test::FakeTexture>(64, 64);
    for (int i = 0; i < count; ++i) {
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 0.25F, 0.25F});
    }
    return sheet;
}

} // namespace

TEST_CASE("the mug meter picks one of eight arc directions from the stick and keeps the last", "[hud][mug]") {
    MugMeter meter;
    CHECK_FALSE(meter.direction());
    // Inside the dead zone nothing is picked yet.
    meter.setStick(0.3F, 0.3F);
    CHECK_FALSE(meter.direction());
    meter.setStick(0.0F, 0.8F);
    CHECK(meter.direction() == 0U);
    meter.setStick(0.05F, -0.9F);
    CHECK(meter.direction() == 1U);
    meter.setStick(-0.9F, 0.0F);
    CHECK(meter.direction() == 2U);
    meter.setStick(0.9F, 0.05F);
    CHECK(meter.direction() == 3U);
    meter.setStick(-0.7F, 0.7F);
    CHECK(meter.direction() == 4U);
    meter.setStick(0.7F, 0.7F);
    CHECK(meter.direction() == 5U);
    meter.setStick(0.7F, -0.7F);
    CHECK(meter.direction() == 6U);
    meter.setStick(-0.7F, -0.7F);
    CHECK(meter.direction() == 7U);
    // Up with x at 0.3 is neither straight up nor a diagonal: the last direction stays.
    meter.setStick(0.3F, 0.9F);
    CHECK(meter.direction() == 7U);
}

TEST_CASE("the mug meter's arcs show by mode and ripple two updates each", "[hud][mug]") {
    MugMeter meter;
    meter.setActive(true);
    meter.setMode(MugMeterMode::Mugging);
    meter.setOnTarget(true);
    // Without a stick update since the last one, no arcs.
    meter.update();
    CHECK_FALSE(meter.arcsShown());
    meter.setStick(0.0F, 1.0F);
    meter.update();
    CHECK(meter.arcsShown());
    CHECK(meter.ripplePhase() == 0U);
    // Two updates a phase: 0, 0, 1, 1, 2, 2, 3, 3, then round again.
    for (std::uint32_t step = 0; step < 5; ++step) {
        meter.setStick(0.0F, 1.0F);
        meter.update();
    }
    CHECK(meter.ripplePhase() == 3U);
    // Off target, mugging hides them; being mugged shows them off target instead.
    meter.setOnTarget(false);
    meter.setStick(0.0F, 1.0F);
    meter.update();
    CHECK_FALSE(meter.arcsShown());
    meter.setMode(MugMeterMode::Mugged);
    meter.setStick(0.0F, 1.0F);
    meter.update();
    CHECK(meter.arcsShown());
}

TEST_CASE("the mug meter clamps its fills and starts empty when shown", "[hud][mug]") {
    MugMeter meter;
    meter.setActive(true);
    meter.setFills(1.5F, -0.2F);
    CHECK(meter.usedFill() == Approx(1.0F));
    CHECK(meter.onTargetFill() == Approx(0.0F));
    meter.setFills(0.25F, 0.5F);
    CHECK(meter.onTargetFill() == Approx(0.5F));
    meter.setActive(false);
    meter.setActive(true);
    CHECK(meter.usedFill() == Approx(0.0F));
}

TEST_CASE("the mug meter draws its bars, stick and lit arcs only while active", "[hud][mug]") {
    const coney::graphics::SpriteSheet minigameSheet = fakeSheet(10);
    const coney::graphics::SpriteSheet partSheet = fakeSheet(60);
    coney::graphics::SpriteBatch minigames(minigameSheet, 16, 10000.0F);
    coney::graphics::SpriteBatch parts(partSheet, 16, 10000.0F);
    coney::hud::HudCanvas canvas;
    canvas.minigames = &minigames;
    canvas.parts = &parts;
    MugMeter meter;
    meter.render(canvas, 0, {});
    CHECK(minigames.sprites().empty());
    CHECK(parts.sprites().empty());

    meter.setActive(true);
    meter.setFills(0.5F, 0.5F);
    meter.setOnTarget(true);
    meter.setStick(1.0F, 0.0F);
    meter.update();
    meter.render(canvas, 0, {});
    // The stick's base and dot, then arc 7 alone in the first phase.
    REQUIRE(minigames.sprites().size() == 3);
    CHECK(minigames.sprites()[0].width == Approx(0.06F));
    CHECK(minigames.sprites()[2].colour == coney::graphics::Rgba{170, 43, 43, 255});
    // Each bar: the left cap, the back, the fill and the right cap; the fill amber on bar 1.
    REQUIRE(parts.sprites().size() == 8);
    CHECK(parts.sprites()[2].colour == coney::graphics::Rgba{128, 100, 0, 255});
}
