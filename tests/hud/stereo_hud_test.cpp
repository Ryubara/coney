// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/stereo_hud.h"

#include <memory>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/recording_device.h"

using Catch::Approx;
using coney::hud::StereoHud;

namespace {

// Three turns a stage, as in the street.
constexpr float kTarget = 6.0F * std::numbers::pi_v<float>;

} // namespace

TEST_CASE("the stereo panel's arrow goes round the backdrop's corners, a quarter turn more each stage",
          "[hud][stereo]") {
    StereoHud panel;
    CHECK_FALSE(panel.shown());
    panel.start(kTarget);
    REQUIRE(panel.shown());
    // Stage 0: top right, unturned; stage 1 top left; 2 bottom left; 3 bottom right.
    CHECK(panel.arrowPlace(0).x == Approx(0.20F));
    CHECK(panel.arrowPlace(0).y == Approx(0.52F));
    CHECK(panel.arrowAngle() == Approx(0.0F));
    panel.setProgress(0.0F, 1);
    CHECK(panel.arrowPlace(0).x == Approx(0.04F));
    CHECK(panel.arrowPlace(0).y == Approx(0.52F));
    CHECK(panel.arrowAngle() == Approx(-std::numbers::pi_v<float> / 2.0F));
    panel.setProgress(0.0F, 2);
    CHECK(panel.arrowPlace(0).y == Approx(0.62F));
    CHECK(panel.arrowAngle() == Approx(-std::numbers::pi_v<float>));
    panel.setProgress(0.0F, 3);
    CHECK(panel.arrowPlace(0).x == Approx(0.20F));
    // Player 1's panel is mirrored about the screen's centre.
    CHECK(panel.arrowPlace(1).x == Approx(0.96F));
    // The stick sits below the backdrop in stages 0-1 and above it in 2-3.
    CHECK(panel.stickPlace(0).y == Approx(0.49F));
    panel.setProgress(0.0F, 0);
    CHECK(panel.stickPlace(0).y == Approx(0.65F));
    CHECK(panel.stickPlace(0).x == Approx(0.105F));
}

TEST_CASE("the stereo gauge grows with the angle turned and drops off as a stage completes", "[hud][stereo]") {
    StereoHud panel;
    panel.start(kTarget);
    CHECK(panel.gaugeSize() == Approx(0.025F));
    panel.setProgress(kTarget / 2.0F, 0);
    CHECK(panel.gaugeSize() == Approx(0.0375F));
    panel.update(0);
    CHECK(panel.drop() == Approx(0.0F));
    // At the target the drop grows by 0.001, 0.002, 0.004 ...
    panel.setProgress(kTarget * 2.0F, 0);
    CHECK(panel.gaugeSize() == Approx(0.05F));
    panel.update(33);
    panel.update(66);
    panel.update(100);
    CHECK(panel.drop() == Approx(0.007F));
    // The next stage puts it back on the arrow.
    panel.setProgress(0.0F, 1);
    panel.update(133);
    CHECK(panel.drop() == Approx(0.0F));
    panel.end();
    CHECK_FALSE(panel.shown());
}

TEST_CASE("the stereo panel's stick cycles its four pictures every 320 ms", "[hud][stereo]") {
    StereoHud panel;
    panel.start(kTarget);
    panel.update(0);
    CHECK(panel.stickRect() == 3);
    panel.update(320);
    CHECK(panel.stickRect() == 2);
    panel.update(959);
    CHECK(panel.stickRect() == 1);
    panel.update(960);
    CHECK(panel.stickRect() == 0);
    panel.update(1280);
    CHECK(panel.stickRect() == 3);
}

TEST_CASE("the stereo panel draws its backdrop, arrow and gauge, then the stick and the ring", "[hud][stereo]") {
    auto texture = std::make_shared<coney::test::FakeTexture>(64, 64);
    coney::graphics::SpriteSheet sheet;
    sheet.texture = texture;
    for (int i = 0; i < 8; ++i) {
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 0.25F, 0.25F});
    }
    coney::graphics::SpriteBatch minigames(sheet, 16, 10000.0F);
    coney::graphics::SpriteBatch parts(sheet, 16, 10000.0F);
    coney::hud::HudCanvas canvas;
    canvas.minigames = &minigames;
    canvas.parts = &parts;
    StereoHud panel;
    panel.render(canvas, 0);
    CHECK(minigames.sprites().empty());
    panel.start(kTarget);
    panel.update(0);
    panel.render(canvas, 0);
    CHECK(minigames.sprites().size() == 3);
    CHECK(parts.sprites().size() == 2);
    // The moving parts are square in overlay units; the gauge at half size.
    CHECK(minigames.sprites()[1].width == Approx(0.12F));
    CHECK(minigames.sprites()[2].height == Approx(0.025F));
    CHECK(parts.sprites()[1].colour == coney::hud::kStereoRingColour);
}
