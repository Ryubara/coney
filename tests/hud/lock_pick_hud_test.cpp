// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/lock_pick_hud.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/recording_device.h"

using Catch::Approx;
using coney::graphics::LogicalPoint;
using coney::graphics::OverlayCamera;
using coney::graphics::OverlayPoint;
using coney::graphics::Rgba;
using coney::graphics::SpriteBatch;
using coney::graphics::SpriteSheet;
using coney::hud::kDeviceInterlaced;
using coney::hud::kDeviceProgressive;
using coney::hud::kDeviceWide;
using coney::hud::LockPickHud;
using coney::hud::LockPickLayout;

namespace {

// A sheet of `count` rectangles over a small fake texture; rectangle 12 (the lock face) spans (0.5, 0.5)-(1, 1).
SpriteSheet fakeSheet(std::size_t count) {
    SpriteSheet sheet;
    sheet.texture = std::make_shared<coney::test::FakeTexture>(64, 64);
    for (std::size_t i = 0; i < count; ++i) {
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 0.25F, 0.25F});
    }
    sheet.page.rects.at(coney::hud::kLockFaceRect) = coney::graphics::UvRect{0.5F, 0.5F, 1.0F, 1.0F};
    return sheet;
}

// The batches a dial draws into.
struct Batches {
    SpriteBatch minigames{fakeSheet(13), 64, 10000.0F};
    SpriteBatch shapes{SpriteSheet{}, 160, 20000.0F};
    SpriteBatch face{fakeSheet(13), 160, 20001.0F};

    [[nodiscard]] coney::hud::HudCanvas canvas() {
        coney::hud::HudCanvas canvas;
        canvas.minigames = &minigames;
        canvas.shapes = &shapes;
        canvas.shapeFace = &face;
        return canvas;
    }
};

} // namespace

TEST_CASE("a lock-pick wedge colours whole rim vertices around 12 o'clock", "[hud][lockpick]") {
    // floor(32 x p / 100 / 2): 35 % gives 5 each side, 9.2 % gives 1, under 3.125 % gives 0.
    CHECK(LockPickHud::wedgeVertices(35.0F) == 5);
    CHECK(LockPickHud::wedgeVertices(27.0F) == 4);
    CHECK(LockPickHud::wedgeVertices(20.0F) == 3);
    CHECK(LockPickHud::wedgeVertices(14.0F) == 2);
    CHECK(LockPickHud::wedgeVertices(9.2F) == 1);
    CHECK(LockPickHud::wedgeVertices(3.0F) == 0);
}

TEST_CASE("a hidden lock-pick dial draws nothing", "[hud][lockpick]") {
    Batches batches;
    LockPickHud dial;
    dial.render(batches.canvas(), 0);
    CHECK(batches.minigames.sprites().empty());
    CHECK(batches.shapes.triangles().empty());
    CHECK(batches.face.triangles().empty());
}

TEST_CASE("a lock-pick wedge is flat-shaded: whole segments, half a segment off 12 o'clock", "[hud][lockpick]") {
    // Fan triangle j (rim vertices j and j + 1) takes rim vertex j + 1's colour, so a wedge of k vertices each side is
    // the 2k + 1 segments from -(k + 1) x 11.25 to +k x 11.25 degrees. Difficulty 1: band 1 (27 %) is j = 0-3 and 27-31
    // (-56.25 to +45 degrees), band 2 (12 %) j = 0 and 30-31 (-22.5 to +11.25).
    const auto covered = [](float percent) {
        std::vector<int> js;
        for (int j = 0; j < coney::hud::kLockSegments; ++j) {
            if (LockPickHud::wedgeSegment(percent, j)) {
                js.push_back(j);
            }
        }
        return js;
    };
    CHECK(covered(27.0F) == std::vector<int>{0, 1, 2, 3, 27, 28, 29, 30, 31});
    CHECK(covered(12.0F) == std::vector<int>{0, 30, 31});
    CHECK(covered(35.0F) == std::vector<int>{0, 1, 2, 3, 4, 26, 27, 28, 29, 30, 31});
    CHECK(covered(14.0F) == std::vector<int>{0, 1, 29, 30, 31});
    CHECK(covered(20.0F) == std::vector<int>{0, 1, 2, 28, 29, 30, 31});
    CHECK(covered(9.2F) == std::vector<int>{0, 30, 31});
    CHECK(covered(0.0F).empty());
}

TEST_CASE("a shown lock-pick dial draws three pins, two flat wedges and the face", "[hud][lockpick]") {
    // Per difficulty: the good wedge's and the perfect wedge's segments (2k + 1 each), clear segments skipped.
    struct Row {
        int difficulty;
        std::size_t good;
        std::size_t perfect;
    };
    for (const Row row : {Row{0, 11, 5}, Row{1, 9, 3}, Row{2, 7, 3}}) {
        CAPTURE(row.difficulty);
        Batches batches;
        LockPickHud dial;
        dial.show(true, row.difficulty);
        dial.render(batches.canvas(), 0);
        // Three pins, largest first, all on one centre; the wedges' triangles; one 32-segment face.
        REQUIRE(batches.minigames.sprites().size() == 3);
        CHECK(batches.minigames.sprites()[0].width == Approx(0.19F));
        CHECK(batches.minigames.sprites()[1].width == Approx(0.152F));
        CHECK(batches.minigames.sprites()[2].width == Approx(0.114F));
        CHECK(batches.minigames.sprites()[2].position.x == batches.minigames.sprites()[0].position.x);
        const std::vector<coney::graphics::OverlayVertex>& tris = batches.shapes.triangles();
        REQUIRE(tris.size() == (row.good + row.perfect) * 3);
        CHECK(batches.face.triangles().size() == std::size_t{32} * 3);
        // Every triangle is one solid colour, the centre's included (so no red haze fades out from the centre), the
        // good wedge's first, then the perfect one's over it; none is clear.
        for (std::size_t t = 0; t < tris.size() / 3; ++t) {
            CAPTURE(t);
            const Rgba expected = t < row.good ? coney::hud::kLockGoodColour : coney::hud::kLockPerfectColour;
            CHECK(tris[3 * t].colour == expected);
            CHECK(tris[(3 * t) + 1].colour == expected);
            CHECK(tris[(3 * t) + 2].colour == expected);
        }
    }

    // Difficulty 1 through a recording device: the first triangle is segment 0, from straight up (rim vertex 0, 40
    // pixels above the centre) clockwise; the good wedge's last is segment 31, ending straight up.
    Batches batches;
    LockPickHud dial;
    dial.show(true, 1);
    dial.render(batches.canvas(), 0);
    coney::test::RecordingDevice device;
    batches.shapes.render(device, coney::graphics::OverlayCamera{});
    REQUIRE(device.triangles.size() == 1);
    const std::vector<coney::graphics::LogicalVertex>& v = device.triangles[0].vertices;
    REQUIRE(v.size() == std::size_t{12} * 3);
    CHECK(v[1].x == Approx(v[0].x).margin(0.01));
    CHECK(v[0].y - v[1].y == Approx(40.0F).margin(0.01));
    CHECK(v[2].x > v[1].x);
    CHECK(v[(3 * 8) + 2].x == Approx(v[0].x).margin(0.01));
    CHECK(v[(3 * 8) + 1].x < v[0].x);
}

TEST_CASE("the lock-pick face maps the rectangle's circle upright", "[hud][lockpick]") {
    Batches batches;
    LockPickHud dial;
    dial.show(true, 2);
    dial.render(batches.canvas(), 0);
    coney::test::RecordingDevice device;
    batches.face.render(device, coney::graphics::OverlayCamera{});
    REQUIRE(device.triangles.size() == 1);
    const std::vector<coney::graphics::LogicalVertex>& v = device.triangles[0].vertices;
    // The centre shows the rectangle's middle; rim vertex 0 (straight up on screen) shows the top of the texture's
    // circle (v = cv - rv), so the keyhole stands upright.
    CHECK(v[0].u == Approx(0.75F));
    CHECK(v[0].v == Approx(0.75F));
    CHECK(v[1].u == Approx(0.75F).margin(1e-5));
    CHECK(v[1].v == Approx(0.5F));
    CHECK(v[1].colour == coney::hud::kLockPinColour);
    // Rim vertex 8 (segment 7's second rim vertex, straight right on screen) shows the circle's right edge.
    CHECK(v[(3 * 7) + 2].u == Approx(1.0F));
    CHECK(v[(3 * 7) + 2].v == Approx(0.75F).margin(1e-5));
    // 41 pixels from the centre.
    CHECK(v[0].y - v[1].y == Approx(41.0F).margin(0.01));
}

TEST_CASE("the lock-pick pins rest at 3.14, not pi", "[hud][lockpick]") {
    // The original's rest angle is the constant 3.14 (docs/research/hud-functions-lua.md#lock-pick-dial-layout).
    LockPickHud dial;
    dial.setPins({1.0F, 2.0F, 3.0F}, 1);
    dial.show(true, 0);
    for (const float angle : dial.pins()) {
        CHECK(angle == 3.14F);
    }
}

TEST_CASE("the lock-pick pins turn by their angles and go once every pin is done", "[hud][lockpick]") {
    Batches batches;
    LockPickHud dial;
    dial.show(true, 1);
    dial.setPins({1.0F, 2.0F, 3.0F}, 3);
    dial.render(batches.canvas(), 1);
    CHECK(batches.minigames.sprites().empty());
    CHECK(batches.shapes.triangles().size() == std::size_t{12} * 3);
    // Player 2's shapes sit right of the screen's middle.
    CHECK(LockPickHud::layout(1, kDeviceInterlaced).shapeCentre.x > 0.0F);
}

TEST_CASE("the lock-pick dial's centres follow the video mode", "[hud][lockpick]") {
    // Interlaced 4:3 (the default): the pins at (-0.56, -0.075), the shapes moved to (-0.565, -0.07) every update.
    const LockPickLayout p1 = LockPickHud::layout(0, kDeviceInterlaced);
    CHECK(p1.pinCentre == OverlayPoint{-0.56F, -0.075F, 1.0F});
    CHECK(p1.shapeCentre == OverlayPoint{-0.565F, -0.07F, 1.0F});
    const LockPickLayout p2 = LockPickHud::layout(1, kDeviceInterlaced);
    CHECK(p2.pinCentre == OverlayPoint{0.545F, -0.075F, 1.0F});
    CHECK(p2.shapeCentre == OverlayPoint{0.546F, -0.07F, 1.0F});
    // Interlaced 16:9: one centre for both layers.
    const std::uint32_t wide = kDeviceInterlaced | kDeviceWide;
    CHECK(LockPickHud::layout(0, wide).pinCentre == OverlayPoint{-0.74F, -0.075F, 1.0F});
    CHECK(LockPickHud::layout(0, wide).shapeCentre == OverlayPoint{-0.74F, -0.075F, 1.0F});
    CHECK(LockPickHud::layout(1, wide).pinCentre == OverlayPoint{0.725F, -0.075F, 1.0F});
    CHECK(LockPickHud::layout(1, wide).shapeCentre == OverlayPoint{0.725F, -0.075F, 1.0F});
    // Progressive 4:3 keeps the static entry; progressive 16:9 has its own.
    CHECK(LockPickHud::layout(0, kDeviceProgressive).pinCentre == OverlayPoint{-0.531F, -0.075F, 1.0F});
    CHECK(LockPickHud::layout(1, kDeviceProgressive).shapeCentre == OverlayPoint{0.519F, -0.075F, 1.0F});
    CHECK(LockPickHud::layout(0, kDeviceProgressive | kDeviceWide).pinCentre == OverlayPoint{-0.62F, -0.075F, 1.0F});
    CHECK(LockPickHud::layout(1, kDeviceProgressive | kDeviceWide).shapeCentre == OverlayPoint{0.62F, -0.075F, 1.0F});
}

TEST_CASE("the lock-pick dial's radii and pin factors follow the video mode", "[hud][lockpick]") {
    // Wedges 40 px and face 41 px, times each mode's (x, y) factors; the pins' width and height factors.
    struct Row {
        std::uint32_t flags;
        float wedgeX, wedgeY, faceX, faceY, pinW, pinH;
    };
    const std::vector<Row> rows{
        {kDeviceInterlaced, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F},
        {kDeviceInterlaced | kDeviceWide, 0.8F, 1.0F, 0.9F, 1.0F, 1.0F, 1.0F},
        {kDeviceProgressive, 0.75F, 1.1F, 0.75F, 1.0F, 0.6F, 0.6F},
        {kDeviceProgressive | kDeviceWide, 0.5F, 0.95F, 0.5F, 0.85F, 0.6F, 0.6F},
    };
    for (const Row& row : rows) {
        CAPTURE(row.flags);
        const LockPickLayout layout = LockPickHud::layout(0, row.flags);
        CHECK(layout.wedgeRadius.x == Approx(40.0F * row.wedgeX));
        CHECK(layout.wedgeRadius.y == Approx(40.0F * row.wedgeY));
        CHECK(layout.faceRadius.x == Approx(41.0F * row.faceX));
        CHECK(layout.faceRadius.y == Approx(41.0F * row.faceY));
        CHECK(layout.pinWidth == Approx(row.pinW));
        CHECK(layout.pinHeight == Approx(row.pinH));
    }
}

TEST_CASE("a lock-pick rim vertex is the centre plus (rx sin a, ry cos a) in pixels", "[hud][lockpick]") {
    // a = pi - j x 2 pi / 32 with y down on screen: rim vertex 0 straight up, 8 to the right (a = pi / 2), 16 straight
    // down, 24 to the left.
    const LogicalPoint radius{32.0F, 40.0F};
    const LogicalPoint up = LockPickHud::rimOffset(radius, 0);
    CHECK(up.x == Approx(0.0F).margin(1e-4));
    CHECK(up.y == Approx(-40.0F));
    const LogicalPoint side = LockPickHud::rimOffset(radius, 8);
    CHECK(side.x == Approx(32.0F));
    CHECK(side.y == Approx(0.0F).margin(1e-4));
    const LogicalPoint down = LockPickHud::rimOffset(radius, 16);
    CHECK(down.x == Approx(0.0F).margin(1e-4));
    CHECK(down.y == Approx(40.0F));
    const LogicalPoint other = LockPickHud::rimOffset(radius, 24);
    CHECK(other.x == Approx(-32.0F));
    // Every rim vertex lies on the ellipse.
    for (int j = 0; j <= coney::hud::kLockSegments; ++j) {
        const LogicalPoint v = LockPickHud::rimOffset(radius, j);
        CHECK(((v.x / 32.0F) * (v.x / 32.0F)) + ((v.y / 40.0F) * (v.y / 40.0F)) == Approx(1.0F));
    }
}

TEST_CASE("the lock-pick pins and shapes are concentric on screen", "[hud][lockpick]") {
    // The pins sit at the centre point at depth 1, not at the GUI position; in the default mode the shapes are 2.2
    // pixels left of and above them, in 16:9 on the same point.
    for (const std::uint32_t flags : {kDeviceInterlaced, kDeviceInterlaced | kDeviceWide}) {
        const OverlayCamera camera = OverlayCamera::forMode((flags & kDeviceWide) != 0);
        for (std::size_t player = 0; player < 2; ++player) {
            CAPTURE(flags, player);
            const LockPickLayout layout = LockPickHud::layout(player, flags);
            const LogicalPoint pins = camera.project(layout.pinCentre);
            const LogicalPoint shapes = camera.project(layout.shapeCentre);
            CHECK(std::abs(pins.x - shapes.x) <= 2.5F);
            CHECK(std::abs(pins.y - shapes.y) <= 2.5F);
        }
    }
    const OverlayCamera camera;
    const LockPickLayout p1 = LockPickHud::layout(0, kDeviceInterlaced);
    CHECK(camera.project(p1.pinCentre).x == Approx(72.8F).margin(0.05));
    CHECK(camera.project(p1.pinCentre).y == Approx(257.6F).margin(0.05));
    CHECK(camera.project(p1.shapeCentre).x - camera.project(p1.pinCentre).x == Approx(-2.2F).margin(0.05));
    CHECK(camera.project(p1.shapeCentre).y - camera.project(p1.pinCentre).y == Approx(-2.24F).margin(0.05));
}

TEST_CASE("the lock-pick pins are drawn at the centre point at depth 1", "[hud][lockpick]") {
    Batches batches;
    LockPickHud dial;
    dial.show(true, 0);
    dial.render(batches.canvas(), 0);
    REQUIRE(batches.minigames.sprites().size() == 3);
    const LockPickLayout layout = LockPickHud::layout(0, kDeviceInterlaced);
    const OverlayCamera camera;
    for (const coney::graphics::Sprite& pin : batches.minigames.sprites()) {
        CHECK(pin.position == layout.pinCentre);
    }
    // The largest pin is 0.19 / (2 x 0.725) of the screen's width across, and square.
    const coney::graphics::Sprite& largest = batches.minigames.sprites()[0];
    const LogicalPoint size = camera.projectSize(largest.width, largest.height, largest.position.z);
    CHECK(size.x == Approx(0.19F / 1.45F * 640.0F));
    CHECK(largest.width == Approx(largest.height));

    // The wedges are centred on the shapes' centre, 2.2 pixels from the pins'.
    coney::test::RecordingDevice device;
    batches.shapes.render(device, camera);
    REQUIRE(device.triangles.size() == 1);
    REQUIRE(!device.triangles[0].vertices.empty());
    const LogicalPoint shapes = camera.project(layout.shapeCentre);
    CHECK(device.triangles[0].vertices[0].x == Approx(shapes.x).margin(0.01));
    CHECK(device.triangles[0].vertices[0].y == Approx(shapes.y).margin(0.01));
}

TEST_CASE("a lock-pick dial in 16:9 narrows its shapes", "[hud][lockpick]") {
    Batches batches;
    LockPickHud dial;
    dial.setVideoMode(kDeviceInterlaced | kDeviceWide);
    dial.show(true, 0);
    dial.render(batches.canvas(), 0);
    const OverlayCamera camera = OverlayCamera::forMode(true);
    coney::test::RecordingDevice device;
    batches.shapes.render(device, camera);
    REQUIRE(device.triangles.size() == 1);
    const std::vector<coney::graphics::LogicalVertex>& v = device.triangles[0].vertices;
    REQUIRE(v.size() > 14);
    // Rim vertex 0 is 40 pixels up; rim vertex 5 (segment 4's second rim vertex, index 3 x 4 + 2, at 56.25 degrees
    // clockwise from 12 o'clock) 32 x sin(56.25 degrees) pixels across.
    CHECK(v[0].y - v[1].y == Approx(40.0F).margin(0.01));
    CHECK(v[14].x - v[0].x == Approx(32.0F * std::sin(5.0F * std::numbers::pi_v<float> / 16.0F)).margin(0.01));
    // The face: 41 pixels up and 36.9 across.
    coney::test::RecordingDevice faceDevice;
    batches.face.render(faceDevice, camera);
    REQUIRE(faceDevice.triangles.size() == 1);
    const std::vector<coney::graphics::LogicalVertex>& f = faceDevice.triangles[0].vertices;
    REQUIRE(f.size() > 26);
    CHECK(f[0].y - f[1].y == Approx(41.0F).margin(0.01));
    CHECK(f[23].x - f[0].x == Approx(36.9F).margin(0.01));
}
