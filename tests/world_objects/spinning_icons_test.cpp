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

TEST_CASE("a dealer's icon floats 2.5 m up and turns half a turn a second from a half turn; others 2.25 m",
          "[spinning_icons]") {
    const Vec3 feet{1.0F, 2.0F, 3.0F};
    const coney::world_objects::IconPose start = spinningIconPose("dyn_flashdeal", feet, 0.0F, 0);
    CHECK(start.position.z == Approx(5.5F));
    // The local half turn about +z.
    CHECK(start.rotation.z == Approx(1.0F));
    CHECK(start.rotation.w == Approx(0.0F).margin(1e-6));
    // Half a second on: three quarters of a turn.
    const coney::world_objects::IconPose later = spinningIconPose("dyn_flashdeal", feet, 0.0F, 500);
    CHECK(later.rotation.z == Approx(std::sin(0.75F * 3.14159265F)));
    CHECK(later.rotation.w == Approx(std::cos(0.75F * 3.14159265F)));
    CHECK(spinningIconPose("dyn_cuffs", feet, 0.0F, 0).position.z == Approx(5.25F));
    // The cuffs turn at half the rate: a quarter turn past the half in a second.
    CHECK(spinningIconPose("dyn_cuffs", feet, 0.0F, 1000).rotation.w == Approx(std::cos(0.75F * 3.14159265F)));
    // The player markers do not turn: they keep the half turn on the human's heading.
    const coney::world_objects::IconPose marker = spinningIconPose("dyn_play_one", feet, 1.0F, 500);
    CHECK(marker.rotation.w == Approx(std::cos((1.0F + 3.14159265F) / 2.0F)));
}

TEST_CASE("dyn_p_one and dyn_p_two spawn the player markers; only dyn_cross shows under the letterbox",
          "[spinning_icons]") {
    using coney::world_objects::iconTypeName;
    CHECK(iconTypeName("dyn_p_one") == "dyn_play_one");
    CHECK(iconTypeName("dyn_p_two") == "dyn_play_two");
    CHECK(iconTypeName("dyn_crown") == "dyn_crown");
    CHECK(coney::world_objects::hiddenByLetterbox("dyn_crown"));
    CHECK_FALSE(coney::world_objects::hiddenByLetterbox("dyn_cross"));
}
