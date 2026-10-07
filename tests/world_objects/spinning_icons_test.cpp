// SPDX-License-Identifier: GPL-3.0-or-later
// The spinning icons over humans (docs/research/ai.md#dealer-icon), on synthetic data.
#include "world_objects/spinning_icons.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::anim::Vec3;
using coney::world_objects::spinningIconPose;

TEST_CASE("each dealer type wears its own icon", "[spinning_icons]") {
    CHECK(coney::world_objects::dealerIcon(0) == "dyn_flashdeal");
    CHECK(coney::world_objects::dealerIcon(1) == "dyn_weapdeal");
    CHECK(coney::world_objects::dealerIcon(2) == "dyn_spraydeal");
    CHECK(coney::world_objects::dealerIcon(3).empty());
}

TEST_CASE("a dealer's icon floats 2.5 m up and turns half a turn a second; others 2.25 m", "[spinning_icons]") {
    const Vec3 feet{1.0F, 2.0F, 3.0F};
    const coney::world_objects::IconPose start = spinningIconPose("dyn_flashdeal", feet, 0);
    CHECK(start.position.z == Approx(5.5F));
    CHECK(start.rotation.w == Approx(1.0F));
    // Half a second: a quarter turn about +z.
    const coney::world_objects::IconPose later = spinningIconPose("dyn_flashdeal", feet, 500);
    CHECK(later.rotation.z == Approx(std::sin(0.25F * 3.14159265F)));
    CHECK(later.rotation.w == Approx(std::cos(0.25F * 3.14159265F)));
    CHECK(spinningIconPose("dyn_cuffs", feet, 0).position.z == Approx(5.25F));
    // The player markers do not turn.
    CHECK(spinningIconPose("dyn_play_one", feet, 500).rotation.w == Approx(1.0F));
}
