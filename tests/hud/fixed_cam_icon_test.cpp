// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/fixed_cam_icon.h"

#include <cstdint>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::Pad;
using coney::PadSample;
using coney::hud::FixedCamIcon;

namespace {

// A pad whose right stick's raw bytes are (`x`, `y`).
Pad padAt(std::uint8_t x, std::uint8_t y) {
    Pad pad;
    PadSample sample{.connected = true};
    sample.sticks[0] = x;
    sample.sticks[1] = y;
    pad.update(sample);
    return pad;
}

} // namespace

TEST_CASE("the right stick counts as pushed outside 64-176", "[hud][fixedcam]") {
    CHECK_FALSE(FixedCamIcon::stickPushed(padAt(128, 128)));
    CHECK_FALSE(FixedCamIcon::stickPushed(padAt(64, 176)));
    CHECK(FixedCamIcon::stickPushed(padAt(63, 128)));
    CHECK(FixedCamIcon::stickPushed(padAt(128, 177)));
}

TEST_CASE("the fixed-camera icon shows while the stick is pushed and fades over a second after", "[hud][fixedcam]") {
    FixedCamIcon icon;
    const Pad still = padAt(128, 128);
    const Pad pushed = padAt(255, 128);
    // A camera that turns with the stick: nothing.
    icon.update(false, &pushed, 0);
    CHECK_FALSE(icon.shown());
    // A camera that ignores it: no push, nothing; a push shows it fully opaque, held or not.
    icon.update(true, &still, 100);
    CHECK_FALSE(icon.shown());
    icon.update(true, &pushed, 200);
    CHECK(icon.shown());
    CHECK(icon.alpha(200) == Approx(1.0F));
    icon.update(true, &pushed, 700);
    CHECK(icon.alpha(700) == Approx(1.0F));
    // Let go after 700: the fade the last push restarted ends at 1,700, so it is half gone at 1,200, then hides.
    icon.update(true, &still, 716);
    CHECK(icon.alpha(1200) == Approx(0.5F));
    icon.update(true, &still, 1716);
    CHECK_FALSE(icon.shown());
    // Back on a turnable camera, a shown icon goes at once.
    icon.update(true, &pushed, 2000);
    icon.update(false, &pushed, 2016);
    CHECK_FALSE(icon.shown());
}

TEST_CASE("the fixed-camera icon draws part_page0 rectangle 87 at (0.9, 0.64) unless turned off", "[hud][fixedcam]") {
    coney::graphics::SpriteSheet sheet;
    sheet.texture = std::make_shared<coney::test::FakeTexture>(512, 256);
    for (int i = 0; i < 100; ++i) {
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 0.05F, 0.1F});
    }
    coney::graphics::SpriteBatch parts(sheet, 8, 10000.0F);
    coney::hud::HudCanvas canvas;
    canvas.parts = &parts;
    FixedCamIcon icon;
    const Pad pushed = padAt(0, 128);
    icon.update(true, &pushed, 0);
    icon.render(canvas, 0);
    REQUIRE(parts.sprites().size() == 1);
    const coney::graphics::OverlayPoint place = coney::graphics::OverlayCamera::guiToOverlay(0.9F, 0.64F);
    CHECK(parts.sprites()[0].position.x == Approx(place.x));
    CHECK(parts.sprites()[0].position.y == Approx(place.y));
    CHECK(parts.sprites()[0].height == Approx(0.09F));
    CHECK(parts.sprites()[0].colour == coney::graphics::Rgba{191, 191, 191, 255});
    parts.clear();
    icon.setEnabled(false);
    icon.render(canvas, 0);
    CHECK(parts.sprites().empty());
}
