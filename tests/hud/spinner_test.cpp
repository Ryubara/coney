// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/spinner.h"

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "hud/hud.h"
#include "support/recording_device.h"

using coney::hud::Spinner;

namespace {

// A part_page0 stand-in with room for rectangle 92.
coney::graphics::SpriteSheet partsSheet() {
    coney::graphics::SpriteSheet sheet;
    sheet.texture = std::make_shared<coney::test::FakeTexture>(64, 64);
    for (int i = 0; i < 100; ++i) {
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 0.25F, 0.25F});
    }
    return sheet;
}

} // namespace

TEST_CASE("the loading pulse fades its red out over 1.1 s and back over the next", "[hud][spinner]") {
    CHECK(Spinner::pulseColour(0).a == 255);
    CHECK(Spinner::pulseColour(550).a == 128);
    CHECK(Spinner::pulseColour(1100).a == 0);
    CHECK(Spinner::pulseColour(1650).a == 128);
    CHECK(Spinner::pulseColour(2200).a == 255);
    CHECK(Spinner::pulseColour(0).r == 221);
}

TEST_CASE("the spinner draws upright after the arrow while shown, in the colour the last pulse left",
          "[hud][spinner]") {
    coney::graphics::SpriteBatch parts(partsSheet(), 16, 11000.0F);
    coney::hud::HudCanvas canvas;
    canvas.parts = &parts;
    coney::hud::Hud hud;
    hud.render(canvas);
    CHECK(parts.sprites().empty());
    // Shown: the set-up's grey at (0.95, 0.83).
    hud.spinner().set(true);
    hud.render(canvas);
    REQUIRE(parts.sprites().size() == 1);
    CHECK(parts.sprites()[0].colour == coney::hud::kSpinnerColour);
    // A pulse leaves its colour on the widget.
    parts.clear();
    hud.spinner().drawPulse(canvas, 0);
    parts.clear();
    hud.render(canvas);
    REQUIRE(parts.sprites().size() == 1);
    CHECK(parts.sprites()[0].colour == coney::hud::kPulseColour);
    // Off, or with the HUD hidden, nothing.
    parts.clear();
    hud.hideAll();
    hud.render(canvas);
    CHECK(parts.sprites().empty());
}
