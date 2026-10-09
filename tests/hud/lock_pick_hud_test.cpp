// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/lock_pick_hud.h"

#include <cstddef>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/recording_device.h"

using Catch::Approx;
using coney::graphics::Rgba;
using coney::graphics::SpriteBatch;
using coney::graphics::SpriteSheet;
using coney::hud::LockPickHud;

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

TEST_CASE("a shown lock-pick dial draws three pins, two wedge fans and the face", "[hud][lockpick]") {
    Batches batches;
    LockPickHud dial;
    dial.show(true, 0);
    dial.render(batches.canvas(), 0);
    // Three pins, largest first, all on one centre; two 32-segment wedges; one 32-segment face.
    REQUIRE(batches.minigames.sprites().size() == 3);
    CHECK(batches.minigames.sprites()[0].width == Approx(0.19F));
    CHECK(batches.minigames.sprites()[1].width == Approx(0.152F));
    CHECK(batches.minigames.sprites()[2].width == Approx(0.114F));
    CHECK(batches.minigames.sprites()[2].position.x == batches.minigames.sprites()[0].position.x);
    CHECK(batches.shapes.triangles().size() == std::size_t{64} * 3);
    CHECK(batches.face.triangles().size() == std::size_t{32} * 3);

    // The good wedge's first segment (straight up) is in colour at its centre and rim, checked through a
    // recording device.
    coney::test::RecordingDevice device;
    batches.shapes.render(device, coney::graphics::OverlayCamera{});
    REQUIRE(device.triangles.size() == 1);
    const std::vector<coney::graphics::LogicalVertex>& v = device.triangles[0].vertices;
    REQUIRE(v.size() == std::size_t{64} * 3);
    // Segment j is vertices 3j (the centre), 3j + 1 and 3j + 2 (rim vertices j and j + 1). With k = 5, segments 0-4 are
    // solid, 5 fades (rim vertex 6 clear), 6-25 are clear at the rim, 26 fades in (rim vertex 27 coloured), 27-31
    // solid.
    const Rgba good = coney::hud::kLockGoodColour;
    const Rgba clear{0, 0, 0, 0};
    CHECK(v[0].colour == good);
    CHECK(v[1].colour == good);
    CHECK(v[14].colour == good);
    CHECK(v[16].colour == good);
    CHECK(v[17].colour == clear);
    CHECK(v[46].colour == clear);
    CHECK(v[45].colour == good);
    CHECK(v[79].colour == clear);
    CHECK(v[80].colour == good);
    // Rim vertex 0 lies straight above the centre, 40 pixels up.
    CHECK(v[1].x == Approx(v[0].x).margin(0.01));
    CHECK(v[0].y - v[1].y == Approx(40.0F).margin(0.01));
}

TEST_CASE("the lock-pick face maps the rectangle's circle, turned a quarter", "[hud][lockpick]") {
    Batches batches;
    LockPickHud dial;
    dial.show(true, 2);
    dial.render(batches.canvas(), 0);
    coney::test::RecordingDevice device;
    batches.face.render(device, coney::graphics::OverlayCamera{});
    REQUIRE(device.triangles.size() == 1);
    const std::vector<coney::graphics::LogicalVertex>& v = device.triangles[0].vertices;
    // The centre shows the rectangle's middle; rim vertex 0 (straight up, angle pi) shows angle 3 pi / 2 of the
    // texture's circle: the rectangle's left edge, half way down.
    CHECK(v[0].u == Approx(0.75F));
    CHECK(v[0].v == Approx(0.75F));
    CHECK(v[1].u == Approx(0.5F));
    CHECK(v[1].v == Approx(0.75F).margin(1e-5));
    CHECK(v[1].colour == coney::hud::kLockPinColour);
    // 41 pixels from the centre.
    CHECK(v[0].y - v[1].y == Approx(41.0F).margin(0.01));
}

TEST_CASE("the lock-pick pins turn by their angles and go once every pin is done", "[hud][lockpick]") {
    Batches batches;
    LockPickHud dial;
    dial.show(true, 1);
    dial.setPins({1.0F, 2.0F, 3.0F}, 3);
    dial.render(batches.canvas(), 1);
    CHECK(batches.minigames.sprites().empty());
    CHECK(batches.shapes.triangles().size() == std::size_t{64} * 3);
    // Player 2's shapes sit right of the screen's middle.
    CHECK(LockPickHud::shapeCentre(1).x > 0.0F);
    CHECK(LockPickHud::pinPlace(1).x == Approx(0.9F));
}
