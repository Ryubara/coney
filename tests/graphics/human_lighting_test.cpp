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

TEST_CASE("Hiding in a shadow dims a human to half over 250 ms; leaving it is at once") {
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
    CHECK(dim.factor() == Approx(1.0F));
}

TEST_CASE("The blob shadow lies 0.05 m above the ground found within 4 m below") {
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(2.0F, 0.0F, 20.0F, 0.0F, 20.0F));
    const auto shadow = coney::graphics::placeBlobShadow(*mesh, Vec3{5.0F, 5.0F, 3.0F});
    REQUIRE(shadow.has_value());
    CHECK(shadow->centre.z == Approx(2.05F));
    CHECK(shadow->normal.z == Approx(1.0F));
    CHECK(shadow->size == Approx(coney::graphics::kBlobShadowSize));
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
