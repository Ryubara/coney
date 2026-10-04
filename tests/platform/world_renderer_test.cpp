// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/world_renderer.h"

#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("the cloud box turns about the up axis one radian a minute", "[world_renderer]") {
    const coney::world::FrameMatrix base{.right = {1, 0, 0}, .up = {0, 1, 0}, .at = {0, 0, 1}, .position = {2, 3, 0}};
    const coney::world::FrameMatrix still = coney::platform::cloudFrame(base, 0);
    CHECK(still.right.x == Approx(1.0F));
    CHECK(still.position.x == Approx(2.0F));
    // A quarter turn takes 60,000 × π/2 ms: x goes to -z, and the up axis stays where it is.
    const auto quarter = static_cast<std::uint64_t>(60000.0 * std::numbers::pi / 2.0);
    const coney::world::FrameMatrix turned = coney::platform::cloudFrame(base, quarter);
    CHECK(turned.right.x == Approx(0.0F).margin(1e-4));
    CHECK(turned.right.z == Approx(-1.0F).margin(1e-4));
    CHECK(turned.up.y == Approx(1.0F));
    CHECK(turned.position.y == Approx(3.0F));
    CHECK(turned.position.z == Approx(-2.0F).margin(1e-3));
}
