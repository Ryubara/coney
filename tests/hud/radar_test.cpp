// SPDX-License-Identifier: GPL-3.0-or-later
// The radar (docs/research/hud.md#the-radar-on-screen, docs/research/gui.md#fn-radarhud): the zoom's easing, where
// blips go, the map under the disc, the disc's triangles and the HUD's radar render, on synthetic data.
#include "hud/radar.h"

#include <cmath>
#include <memory>
#include <numbers>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/sprite_batch.h"
#include "hud/hud.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::hud::RadarState;
using coney::hud::RadarView;

namespace {

constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0F;

// A view of a player at (10, 20) whose camera faces `heading` (0 faces +y, positive to the left).
RadarView viewFacing(float heading) {
    RadarView view;
    view.known = true;
    view.position = Vec3{10.0F, 20.0F, 0.0F};
    view.cameraHeading = heading;
    return view;
}

// A sheet of `rects` rectangles of 1/16 × 1/16 over a fake texture of `width` × `height`.
coney::graphics::SpriteSheet sheetOf(std::size_t rects, int width, int height) {
    coney::graphics::SpriteSheet sheet;
    for (std::size_t i = 0; i < rects; ++i) {
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 1.0F / 16.0F, 1.0F / 16.0F});
    }
    sheet.texture = std::make_shared<coney::test::FakeTexture>(width, height);
    return sheet;
}

} // namespace

TEST_CASE("the radar's zoom eases toward the rest radius standing and the fast one at 12 m/s", "[hud][radar]") {
    RadarState radar;
    radar.zoom = 50.0F;
    // Standing: already at rest.
    CHECK(coney::hud::radarZoomStep(radar, 0.0F, 33) == Approx(50.0F));
    // Full speed for 100 ms: 4 % of the way to 75.
    CHECK(coney::hud::radarZoomStep(radar, 20.0F, 100) == Approx(51.0F));
    // A long step goes all the way; the scale multiplies the target.
    radar.zoomScale = 0.5F;
    CHECK(coney::hud::radarZoomStep(radar, 6.0F, 5000) == Approx(31.25F));
}

TEST_CASE("a blip goes ahead of the player up the disc and to his right across it", "[hud][radar]") {
    // The camera faces +y: +y is up, +x is right.
    const RadarView north = viewFacing(0.0F);
    const coney::hud::RadarOffset ahead = coney::hud::radarBlipOffset(north, 50.0F, Vec3{10.0F, 45.0F, 0.0F});
    CHECK(ahead.x == Approx(0.0F).margin(1e-6));
    CHECK(ahead.y == Approx(0.06F));
    const coney::hud::RadarOffset right = coney::hud::radarBlipOffset(north, 50.0F, Vec3{35.0F, 20.0F, 0.0F});
    CHECK(right.x == Approx(0.06F));
    CHECK(right.y == Approx(0.0F).margin(1e-6));
    // The camera turned a quarter to the left (facing −x): a point to the west is ahead.
    const RadarView west = viewFacing(kHalfPi);
    const coney::hud::RadarOffset turned = coney::hud::radarBlipOffset(west, 50.0F, Vec3{-15.0F, 20.0F, 0.0F});
    CHECK(turned.x == Approx(0.0F).margin(1e-6));
    CHECK(turned.y == Approx(0.06F));
    // Beyond the zoom: on the edge, 0.9 × 0.12 along its direction.
    const coney::hud::RadarOffset far = coney::hud::radarBlipOffset(north, 50.0F, Vec3{10.0F, -480.0F, 0.0F});
    CHECK(far.y == Approx(-0.108F));
}

TEST_CASE("the map under the disc's centre is the player's place through the level's floats", "[hud][radar]") {
    const coney::hud::RadarMap map{.sheet = "level99", .offsetX = -1.33F, .offsetY = 61.55F, .scale = 95.0F};
    const RadarView view = viewFacing(0.0F);
    const std::array<float, 2> centre = coney::hud::radarMapUv(map, view, 50.0F, 0.0F, 0.0F);
    CHECK(centre[0] == Approx((10.0F - 1.33F) / 95.0F));
    CHECK(centre[1] == Approx((61.55F - 20.0F) / 95.0F));
    // The top of the disc is 50 m ahead (+y): v falls by 50 / 95.
    const std::array<float, 2> top = coney::hud::radarMapUv(map, view, 50.0F, 0.0F, 1.0F);
    CHECK(top[0] == Approx(centre[0]));
    CHECK(top[1] == Approx(centre[1] - (50.0F / 95.0F)));
}

TEST_CASE("the disc is 32 segments: a filled part and a ring fading to nothing", "[hud][radar]") {
    coney::graphics::SpriteBatch batch(sheetOf(1, 256, 256), 0, 0.0F);
    RadarState radar;
    radar.map = coney::hud::RadarMap{.sheet = "level99", .offsetX = 0.0F, .offsetY = 0.0F, .scale = 100.0F};
    radar.view = viewFacing(0.0F);
    const coney::graphics::OverlayPoint centre{0.5F, -0.3F, 1.0F};
    coney::hud::addRadarDisc(batch, centre, 0.125F, 0.11F, radar, coney::hud::kRadarDiscColour);
    const auto& corners = batch.triangles();
    REQUIRE(corners.size() == static_cast<std::size_t>(coney::hud::kRadarSegments) * 3 * 3);
    // The first triangle: the centre, then the filled part's edge, starting at the top.
    CHECK(corners[0].position.x == Approx(0.5F));
    CHECK(corners[0].colour.a == 240);
    CHECK(corners[1].position.x == Approx(0.5F).margin(1e-6));
    CHECK(corners[1].position.y == Approx(-0.3F + (0.825F * 0.11F)));
    // The ring's outer corners have no alpha.
    CHECK(corners[4].colour.a == 0);
    CHECK(corners[4].position.y == Approx(-0.3F + 0.11F));
}

TEST_CASE("blips blink as new objectives and while flashing; enemies show only when marked", "[hud][radar]") {
    coney::hud::RadarBlip blip;
    CHECK(coney::hud::radarBlipShown(blip, 0));
    blip.flashCountdown = 10;
    CHECK(coney::hud::radarBlipShown(blip, 0));
    CHECK_FALSE(coney::hud::radarBlipShown(blip, 4));
    CHECK(coney::hud::radarBlipShown(blip, 8));
    blip.flashCountdown = 0;
    blip.type = 6;
    CHECK_FALSE(coney::hud::radarBlipShown(blip, 0));
}

TEST_CASE("a dot's size is twice its size over its rectangle, the height by the texture's shape and 0.925",
          "[hud][radar]") {
    // Icon 365 of part_page0 (11 × 11 texels of 512 × 256) at 0.7 × 0.8: about 0.024 units across.
    const coney::graphics::UvRect uv{0.0F, 0.0F, 11.0F / 512.0F, 11.0F / 256.0F};
    const std::array<float, 2> size = coney::hud::radarDotSize(uv, 512.0F, 256.0F, 0.56F);
    CHECK(size[0] == Approx(2.0F * 0.56F * 11.0F / 512.0F));
    CHECK(size[1] == Approx(2.0F * 0.56F * (11.0F / 256.0F) * 0.5F * 0.925F));
}

TEST_CASE("the HUD draws the map disc, the located blips and the player's arrow", "[hud][radar]") {
    coney::hud::Hud hud;
    hud.setRadarMap(coney::hud::RadarMap{.sheet = "level99", .offsetX = 0.0F, .offsetY = 100.0F, .scale = 100.0F});
    hud.radar().blips[7.0] = coney::hud::RadarBlip{.type = 7, .icon = 365};
    hud.radar().blips[8.0] = coney::hud::RadarBlip{.type = 10, .icon = 69};
    // Only handle 7 is found.
    hud.setRadarLocator([](double handle) -> std::optional<Vec3> {
        return handle == 7.0 ? std::optional(Vec3{10.0F, 30.0F, 0.0F}) : std::nullopt;
    });
    coney::hud::HudFrame frame;
    frame.nowMs = 1000;
    frame.radar = viewFacing(0.0F);
    hud.update(frame);
    coney::graphics::SpriteBatch map(sheetOf(1, 256, 256), 0, 0.0F);
    coney::graphics::SpriteBatch parts(sheetOf(371, 512, 256), 64, 10000.0F);
    coney::hud::HudCanvas canvas;
    canvas.radarMap = &map;
    canvas.parts = &parts;
    hud.render(canvas);
    CHECK(map.triangles().size() == static_cast<std::size_t>(coney::hud::kRadarSegments) * 9);
    // The one located blip, 10 m ahead, and the arrow's two triangles.
    REQUIRE(parts.sprites().size() == 1);
    CHECK(parts.sprites()[0].position.x == Approx(coney::hud::kRadarX));
    CHECK(parts.sprites()[0].position.y == Approx(coney::hud::kRadarY + (10.0F * 0.12F / 50.0F)));
    CHECK(parts.triangles().size() == 6);
}

TEST_CASE("without a usable map the radar draws no disc", "[hud][radar]") {
    coney::hud::Hud hud;
    coney::hud::HudFrame frame;
    frame.radar = viewFacing(0.0F);
    hud.update(frame);
    coney::graphics::SpriteBatch map(sheetOf(1, 256, 256), 0, 0.0F);
    coney::hud::HudCanvas canvas;
    canvas.radarMap = &map;
    hud.render(canvas);
    CHECK(map.triangles().empty());
}
