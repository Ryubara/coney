// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/num_indicator.h"

#include <array>
#include <cstddef>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hud/hud.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::Language;
using coney::hud::GuiPoint;
using coney::hud::NumIndicator;

namespace {

// A sheet of `rects` equal rectangles over a 512 × 256 texture.
coney::graphics::SpriteSheet sheetOf(std::size_t rects) {
    coney::graphics::SpriteSheet sheet;
    sheet.texture = std::make_shared<coney::test::FakeTexture>(512, 256);
    for (std::size_t i = 0; i < rects; ++i) {
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 0.05F, 0.1F});
    }
    return sheet;
}

} // namespace

TEST_CASE("the tally marks sit in fours with a bar across each, right of the header", "[hud][numindicator]") {
    // The page's table: x from 0.125 every 0.015, the fifth a bar at 0.145, the second group from 0.195; y 0.944.
    constexpr std::array<float, 9> kX{0.125F, 0.140F, 0.155F, 0.170F, 0.145F, 0.195F, 0.210F, 0.225F, 0.240F};
    for (std::size_t i = 0; i < kX.size(); ++i) {
        const GuiPoint place = NumIndicator::markPlace(i, Language::English);
        CHECK(place.x == Approx(kX.at(i)).margin(1e-5));
        CHECK(place.y == Approx(0.944F).margin(1e-5));
        CHECK(NumIndicator::isBar(i) == (i == 4));
    }
    const GuiPoint header = NumIndicator::headerPlace(Language::English);
    CHECK(header.x == Approx(0.06F).margin(1e-5));
    CHECK(header.y == Approx(0.944F).margin(1e-5));
    // German: the origin 0.05 right and the header's shift 0.15, so the marks move 0.12 and the header 0.05.
    CHECK(NumIndicator::markPlace(0, Language::German).x == Approx(0.245F).margin(1e-5));
    CHECK(NumIndicator::headerPlace(Language::German).x == Approx(0.11F).margin(1e-5));
    CHECK(NumIndicator::headerRecord(Language::English) == 0x20e);
    CHECK(NumIndicator::headerRecord(Language::French) == 0x20f);
    CHECK(NumIndicator::headerRecord(Language::German) == 0x210);
}

TEST_CASE("the shared indicator counts a gang's living members, only in a Rumble level", "[hud][numindicator]") {
    coney::graphics::SpriteBatch parts(sheetOf(400), 64, 10000.0F);
    coney::graphics::SpriteBatch header(sheetOf(1), 2, 11000.0F);
    coney::graphics::SpriteBatch shadows(sheetOf(1), 4, 10000.0F);
    coney::hud::HudCanvas canvas;
    canvas.parts = &parts;
    std::uint32_t headerRecord = 0;
    canvas.banner = [&](std::uint32_t record, bool shadow) {
        headerRecord = record;
        return shadow ? &shadows : &header;
    };
    coney::hud::Hud hud;
    hud.setNumIndicator(2, true, 3);
    int living = 7;
    coney::hud::HudFrame frame;
    frame.levelNumber = 101;
    frame.gangLiving = [&living](int gang) { return gang == 3 ? living : 0; };
    hud.update(frame);
    CHECK(hud.numIndicator(2).count == 7);
    hud.render(canvas);
    // The header and its two shadows, then seven marks, blue and turned.
    CHECK(headerRecord == 0x20e);
    REQUIRE(header.sprites().size() == 1);
    CHECK(header.sprites()[0].colour == coney::hud::kNumIndicatorColour);
    CHECK(shadows.sprites().size() == 2);
    REQUIRE(parts.sprites().size() == 7);
    const coney::graphics::OverlayPoint bar = coney::graphics::OverlayCamera::guiToOverlay(0.145F, 0.944F);
    CHECK(parts.sprites()[4].position.x == Approx(bar.x));
    CHECK(parts.sprites()[4].height == Approx(coney::hud::kTallyBarSize));
    CHECK(parts.sprites()[0].height == Approx(coney::hud::kTallyStrokeSize));

    // A gang larger than nine draws the nine sprites there are.
    living = 12;
    hud.update(frame);
    parts.clear();
    hud.render(canvas);
    CHECK(parts.sprites().size() == 9);

    // Outside a Rumble level the shared indicator is not drawn; off, nothing is.
    frame.levelNumber = 99;
    hud.update(frame);
    parts.clear();
    hud.render(canvas);
    CHECK(parts.sprites().empty());
    frame.levelNumber = 101;
    hud.setNumIndicator(2, true, -1);
    hud.update(frame);
    parts.clear();
    hud.render(canvas);
    CHECK(parts.sprites().empty());
}
