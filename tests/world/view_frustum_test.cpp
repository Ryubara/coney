// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/view_frustum.h"

#include <catch2/catch_test_macros.hpp>

#include "world/debug_camera.h"

using coney::world::Box;
using coney::world::CameraPose;
using coney::world::ViewFrustum;

namespace {

// A unit box centred on (x, y, z).
Box boxAt(float x, float y, float z) { return Box{{x - 0.5F, y - 0.5F, z - 0.5F}, {x + 0.5F, y + 0.5F, z + 0.5F}}; }

} // namespace

TEST_CASE("the frustum keeps boxes in front of the camera within its clip distances and window", "[view_frustum]") {
    // At the origin looking along +z, a 90-degree window both ways, clip from 1 to 100.
    const ViewFrustum frustum(CameraPose{}, 1.0F, 1.0F, 1.0F, 100.0F);
    CHECK(frustum.mayContain(boxAt(0.0F, 0.0F, 10.0F)));
    CHECK(frustum.mayContain(boxAt(9.0F, 0.0F, 10.0F)));                                   // near the side edge
    CHECK_FALSE(frustum.mayContain(boxAt(0.0F, 0.0F, -10.0F)));                            // behind
    CHECK_FALSE(frustum.mayContain(boxAt(0.0F, 0.0F, 200.0F)));                            // beyond the far clip
    CHECK_FALSE(frustum.mayContain(boxAt(20.0F, 0.0F, 10.0F)));                            // off to the side
    CHECK_FALSE(frustum.mayContain(boxAt(0.0F, 20.0F, 10.0F)));                            // above
    CHECK(frustum.mayContain(Box{{-1000.0F, -1.0F, -1000.0F}, {1000.0F, 1.0F, 1000.0F}})); // around the camera
}

TEST_CASE("the frustum follows the camera's turn", "[view_frustum]") {
    coney::world::DebugCamera camera({0.0F, 0.0F, 0.0F});
    camera.setOrientation(1.5707964F, 0.0F); // a quarter turn: looking along +x
    const ViewFrustum frustum(camera.pose(), 0.5F, 0.5F, 0.5F, 50.0F);
    CHECK(frustum.mayContain(boxAt(10.0F, 0.0F, 0.0F)));
    CHECK_FALSE(frustum.mayContain(boxAt(0.0F, 0.0F, 10.0F)));
}
