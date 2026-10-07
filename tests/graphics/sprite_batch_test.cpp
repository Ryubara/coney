// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/sprite_batch.h"

#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/overlay_camera.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::graphics::OverlayCamera;
using coney::graphics::OverlayPass;
using coney::graphics::Sprite;
using coney::graphics::SpriteBatch;
using coney::graphics::SpriteSheet;
using coney::graphics::UvRect;
using coney::test::FakeTexture;
using coney::test::RecordingDevice;

namespace {

// A sheet of two rectangles over a fake 64 x 64 texture.
SpriteSheet twoRectSheet(const std::shared_ptr<FakeTexture>& texture) {
    SpriteSheet sheet;
    sheet.page.rects = {UvRect{0.0F, 0.0F, 0.5F, 0.5F}, UvRect{0.5F, 0.5F, 1.0F, 1.0F}};
    sheet.texture = texture;
    return sheet;
}

// A sprite at the GUI centre, `size` overlay units square.
Sprite centred(float size, UvRect uv) {
    return Sprite{OverlayCamera::guiToOverlay(0.5F, 0.5F), size, size, uv, coney::graphics::kWhite};
}

} // namespace

TEST_CASE("a sprite batch drops sprites past its capacity", "[sprite_batch]") {
    SpriteBatch batch(twoRectSheet(std::make_shared<FakeTexture>(64, 64)), 2, 9000.0F);
    CHECK(batch.addSprite(centred(0.1F, UvRect{})));
    CHECK(batch.addSprite(centred(0.1F, UvRect{})));
    CHECK_FALSE(batch.addSprite(centred(0.1F, UvRect{})));
    CHECK(batch.sprites().size() == 2);
    CHECK(batch.mostSprites() == 2);
    batch.clear();
    CHECK(batch.sprites().empty());
    CHECK(batch.mostSprites() == 2);
    CHECK(batch.addSprite(centred(0.1F, UvRect{})));
}

TEST_CASE("a batch draws its sprites centred on their projected positions", "[sprite_batch]") {
    auto texture = std::make_shared<FakeTexture>(64, 64);
    SpriteBatch batch(twoRectSheet(texture), 4, 9000.0F);
    const OverlayCamera camera;
    const UvRect uv{0.5F, 0.5F, 1.0F, 1.0F};
    batch.addSprite(centred(0.22F, uv));
    RecordingDevice device;
    batch.render(device, camera);
    REQUIRE(device.draws.size() == 1);
    CHECK(device.draws[0].texture == texture.get());
    REQUIRE(device.draws[0].quads.size() == 1);
    const coney::graphics::LogicalQuad& quad = device.draws[0].quads[0];
    const coney::graphics::LogicalPoint size = camera.projectSize(0.22F, 0.22F, OverlayCamera::kGuiDepth);
    CHECK(quad.width == Approx(size.x));
    CHECK(quad.height == Approx(size.y));
    CHECK(quad.x + quad.width / 2 == Approx(320.0));
    CHECK(quad.y + quad.height / 2 == Approx(224.0));
    CHECK(quad.uv == uv);
}

TEST_CASE("an empty batch draws nothing", "[sprite_batch]") {
    SpriteBatch batch(twoRectSheet(std::make_shared<FakeTexture>(64, 64)), 4, 0.0F);
    RecordingDevice device;
    batch.render(device, OverlayCamera{});
    CHECK(device.draws.empty());
}

TEST_CASE("the 2D pass draws batches by ascending key, then empties them", "[sprite_batch]") {
    auto first = std::make_shared<FakeTexture>(8, 8);
    auto second = std::make_shared<FakeTexture>(16, 16);
    auto third = std::make_shared<FakeTexture>(32, 32);
    SpriteBatch top(twoRectSheet(first), 4, 11000.0F);
    SpriteBatch bottom(twoRectSheet(second), 4, 8500.0F);
    SpriteBatch middle(twoRectSheet(third), 4, 9000.0F);
    SpriteBatch empty(twoRectSheet(third), 4, 1.0F);
    for (SpriteBatch* batch : {&top, &bottom, &middle}) {
        batch->addSprite(centred(0.1F, UvRect{}));
    }
    OverlayPass pass;
    pass.queue(top);
    pass.queue(bottom);
    pass.queue(middle);
    pass.queue(empty);
    CHECK(pass.queued() == 4);
    RecordingDevice device;
    pass.render(device, OverlayCamera{});
    // The smallest key first, the largest last (on top); the empty batch draws nothing.
    REQUIRE(device.draws.size() == 3);
    CHECK(device.draws[0].texture == second.get());
    CHECK(device.draws[1].texture == third.get());
    CHECK(device.draws[2].texture == first.get());
    CHECK(top.sprites().empty());
    CHECK(bottom.sprites().empty());
    CHECK(pass.queued() == 0);
}

TEST_CASE("batches with equal keys keep the order they were queued in", "[sprite_batch]") {
    auto a = std::make_shared<FakeTexture>(8, 8);
    auto b = std::make_shared<FakeTexture>(8, 8);
    SpriteBatch first(twoRectSheet(a), 1, 5.0F);
    SpriteBatch second(twoRectSheet(b), 1, 5.0F);
    first.addSprite(centred(0.1F, UvRect{}));
    second.addSprite(centred(0.1F, UvRect{}));
    OverlayPass pass;
    pass.queue(second);
    pass.queue(first);
    RecordingDevice device;
    pass.render(device, OverlayCamera{});
    REQUIRE(device.draws.size() == 2);
    CHECK(device.draws[0].texture == b.get());
    CHECK(device.draws[1].texture == a.get());
}

TEST_CASE("a batch draws its triangles after its sprites, projected, with its addressing", "[sprite_batch]") {
    auto texture = std::make_shared<FakeTexture>(64, 64);
    SpriteBatch batch(twoRectSheet(texture), 4, 0.0F);
    batch.setTriangleStates(coney::graphics::TriangleStates{.wrap = true, .alphaRef = 0.5F});
    const OverlayCamera camera;
    const coney::graphics::OverlayPoint centre = OverlayCamera::guiToOverlay(0.5F, 0.5F);
    const coney::graphics::OverlayVertex corner{centre, 1.5F, -0.5F, coney::graphics::kWhite};
    batch.addSprite(centred(0.1F, UvRect{}));
    batch.addTriangle(corner, corner, corner);
    // A corner behind the camera drops its triangle.
    coney::graphics::OverlayVertex behind = corner;
    behind.position.z = -1.0F;
    batch.addTriangle(corner, behind, corner);
    REQUIRE(batch.triangles().size() == 3);
    RecordingDevice device;
    batch.render(device, camera);
    REQUIRE(device.calls == std::vector<std::string>{"draw", "triangles"});
    REQUIRE(device.triangles.size() == 1);
    CHECK(device.triangles[0].states.wrap);
    CHECK(device.triangles[0].states.alphaRef == 0.5F);
    CHECK(device.triangles[0].texture == texture.get());
    REQUIRE(device.triangles[0].vertices.size() == 3);
    CHECK(device.triangles[0].vertices[0].x == Approx(320.0));
    CHECK(device.triangles[0].vertices[0].y == Approx(224.0));
    CHECK(device.triangles[0].vertices[0].u == Approx(1.5));
    batch.clear();
    CHECK(batch.triangles().empty());
}

TEST_CASE("a turned sprite is drawn as two triangles, turned clockwise on screen", "[sprite_batch]") {
    SpriteBatch batch(twoRectSheet(std::make_shared<FakeTexture>(64, 64)), 4, 9000.0F);
    const OverlayCamera camera;
    batch.addSprite(centred(0.2F, UvRect{}), std::numbers::pi_v<float> / 2.0F);
    RecordingDevice device;
    batch.render(device, camera);
    CHECK(device.draws.empty());
    REQUIRE(device.triangles.size() == 1);
    // Top left, top right, bottom right; top left, bottom right, bottom left (of the texture).
    const std::vector<coney::graphics::LogicalVertex>& corners = device.triangles[0].vertices;
    REQUIRE(corners.size() == 6);
    CHECK(corners[3].x == corners[0].x);
    CHECK(corners[4].y == corners[2].y);
    // A quarter turn clockwise: the texture's top-left corner goes to the top right on screen.
    const coney::graphics::LogicalPoint size = camera.projectSize(0.2F, 0.2F, OverlayCamera::kGuiDepth);
    CHECK(corners[0].x == Approx(320.0 + size.x / 2));
    CHECK(corners[0].y == Approx(224.0 - size.y / 2));
    CHECK(corners[0].u == 0.0F);
    CHECK(corners[1].x == Approx(320.0 + size.x / 2));
    CHECK(corners[1].y == Approx(224.0 + size.y / 2));
}

TEST_CASE("a batch keeps the order of its turned and unturned sprites", "[sprite_batch]") {
    SpriteBatch batch(twoRectSheet(std::make_shared<FakeTexture>(64, 64)), 4, 9000.0F);
    batch.addSprite(centred(0.1F, UvRect{}));
    batch.addSprite(centred(0.1F, UvRect{}), 1.0F);
    batch.addSprite(centred(0.1F, UvRect{}));
    RecordingDevice device;
    batch.render(device, OverlayCamera{});
    CHECK(device.calls == std::vector<std::string>{"draw", "triangles", "draw"});
    batch.clear();
    CHECK(batch.sprites().empty());
}
