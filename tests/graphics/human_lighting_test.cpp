// SPDX-License-Identifier: GPL-3.0-or-later
// A human's lighting (docs/research/lighting.md#humans): the dimming while hidden in a shadow and the blob shadow's
// ground ray, on synthetic collision meshes.
#include "graphics/human_lighting.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/collision_fixtures.h"

using Catch::Approx;
using coney::graphics::ShadowDim;
using coney::raycast::Vec3;

TEST_CASE("Hiding in a shadow dims a human to half over 250 ms; leaving it brightens him back over 250 ms") {
    ShadowDim dim;
    CHECK(dim.factor() == Approx(1.0F));
    dim.step(true, 33);
    CHECK(dim.hidden());
    CHECK(dim.factor() == Approx(1.0F));
    dim.step(true, 125);
    CHECK(dim.factor() == Approx(0.75F));
    dim.step(true, 1000);
    CHECK(dim.factor() == Approx(0.5F));
    dim.step(false, 33);
    CHECK_FALSE(dim.hidden());
    CHECK(dim.factor() == Approx(0.5F));
    dim.step(false, 125);
    CHECK(dim.factor() == Approx(0.75F));
    dim.step(false, 1000);
    CHECK(dim.factor() == Approx(1.0F));
}

TEST_CASE("The blob shadow lies 0.05 m above the ground found within 4 m below") {
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(2.0F, 0.0F, 20.0F, 0.0F, 20.0F));
    const auto shadow = coney::graphics::placeBlobShadow(*mesh, Vec3{5.0F, 5.0F, 3.0F});
    REQUIRE(shadow.has_value());
    const coney::graphics::BlobShadow placed = shadow.value_or(coney::graphics::BlobShadow{});
    CHECK(placed.centre.z == Approx(2.05F));
    CHECK(placed.normal.z == Approx(1.0F));
    CHECK(placed.size == Approx(coney::graphics::kBlobShadowSize));
    // Ground 4.5 m below the ray's start: none.
    CHECK_FALSE(coney::graphics::placeBlobShadow(*mesh, Vec3{5.0F, 5.0F, 6.3F}).has_value());
}

TEST_CASE("Ground with type bit 4 is a shadow to hide in") {
    const auto plain = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 20.0F, 0.0F, 20.0F));
    CHECK_FALSE(coney::graphics::onShadowGround(*plain, Vec3{5.0F, 5.0F, 0.1F}));
    const auto dark = coney::test::makeMesh(
        coney::test::floorAt(0.0F, 0.0F, 20.0F, 0.0F, 20.0F, 1, coney::graphics::kTriangleShadow));
    CHECK(coney::graphics::onShadowGround(*dark, Vec3{5.0F, 5.0F, 0.1F}));
}

TEST_CASE("the blob shadow is a near-black sprite at half alpha on RenderWare's 0-255 scale", "[human_lighting]") {
    // (10, 10, 10, 128): drawn as given, 128 of 255 is half transparent (docs/research/lighting.md#humans).
    CHECK(coney::graphics::kBlobShadowColour[3] == 128);
    CHECK(static_cast<float>(coney::graphics::kBlobShadowColour[3]) / 255.0F == Catch::Approx(0.5F).margin(0.01F));
    CHECK(coney::graphics::kBlobShadowColour[0] == 10);
}

TEST_CASE("A human fades out from 60 to 70 m from the camera") {
    CHECK(coney::graphics::humanDistanceFade(10.0F) == Approx(1.0F));
    CHECK(coney::graphics::humanDistanceFade(60.0F) == Approx(1.0F));
    CHECK(coney::graphics::humanDistanceFade(65.0F) == Approx(0.5F));
    CHECK(coney::graphics::humanDistanceFade(70.0F) == Approx(0.0F));
    CHECK(coney::graphics::humanDistanceFade(90.0F) == Approx(0.0F));
}

TEST_CASE("A human is culled before posing only when no pose of his can be in the picture") {
    using coney::graphics::cullHuman;
    using coney::graphics::HumanCull;
    using W = coney::world::Vec3;
    // A camera at the origin looking down +z (RenderWare's axes, y up), its view window 0.5 × 0.375 at distance 1. The
    // sphere that holds his poses is 3 m round a point 1 m above his feet (kHumanPoseRadius, kHumanPoseLift).
    const coney::world::CameraPose camera;
    const auto cull = [&camera](W feet, float scale = 1.0F) { return cullHuman(camera, 0.5F, 0.375F, feet, scale); };
    CHECK(cull(W{0.0F, -1.0F, 10.0F}) == HumanCull::MayShow);
    // Past the draw distance (70 m) by more than the sphere: no body of his can be drawn. Within 3 m of it, one may.
    CHECK(cull(W{0.0F, -1.0F, 80.0F}) == HumanCull::PastDrawDistance);
    CHECK(cull(W{0.0F, -1.0F, 72.5F}) == HumanCull::MayShow);
    // Behind the camera by more than the sphere: out of view; 2 m behind, his arm may still reach into it.
    CHECK(cull(W{0.0F, -1.0F, -5.0F}) == HumanCull::OutOfView);
    CHECK(cull(W{0.0F, -1.0F, -2.0F}) == HumanCull::MayShow);
    // Beside the view: 10 m ahead the side is 5 m across, and the sphere reaches 3 m from the side's slanted plane,
    // 3 × sqrt(1 + 0.5²) = 3.35 m across: 8 m is in, 8.5 m is out, on either side.
    CHECK(cull(W{8.0F, -1.0F, 10.0F}) == HumanCull::MayShow);
    CHECK(cull(W{8.5F, -1.0F, 10.0F}) == HumanCull::OutOfView);
    CHECK(cull(W{-8.5F, -1.0F, 10.0F}) == HumanCull::OutOfView);
    CHECK(cull(W{30.0F, -1.0F, 10.0F}) == HumanCull::OutOfView);
    // Above the view: 10 m ahead the top is 3.75 m up, plus 3 × sqrt(1 + 0.375²) = 3.2 m.
    CHECK(cull(W{0.0F, 5.5F, 10.0F}) == HumanCull::MayShow);
    CHECK(cull(W{0.0F, 20.0F, 10.0F}) == HumanCull::OutOfView);
    // Behind and far: the view is tested first.
    CHECK(cull(W{0.0F, -1.0F, -100.0F}) == HumanCull::OutOfView);
    // A body drawn twice as big holds his poses in twice the sphere; a smaller one keeps the 3 m.
    CHECK(cull(W{0.0F, -1.0F, -5.0F}, 2.0F) == HumanCull::MayShow);
    CHECK(cull(W{0.0F, -1.0F, -5.0F}, 0.5F) == HumanCull::OutOfView);
    CHECK(cull(W{0.0F, -1.0F, 75.0F}, 2.0F) == HumanCull::MayShow);
    // A camera elsewhere, looking down +x.
    const coney::world::CameraPose turned{.position = W{100.0F, 0.0F, 0.0F},
                                          .forward = W{1.0F, 0.0F, 0.0F},
                                          .up = W{0.0F, 1.0F, 0.0F},
                                          .right = W{0.0F, 0.0F, 1.0F}};
    CHECK(cullHuman(turned, 0.5F, 0.375F, W{110.0F, -1.0F, 0.0F}, 1.0F) == HumanCull::MayShow);
    CHECK(cullHuman(turned, 0.5F, 0.375F, W{90.0F, -1.0F, 0.0F}, 1.0F) == HumanCull::OutOfView);
    CHECK(cullHuman(turned, 0.5F, 0.375F, W{180.0F, -1.0F, 0.0F}, 1.0F) == HumanCull::PastDrawDistance);
}
