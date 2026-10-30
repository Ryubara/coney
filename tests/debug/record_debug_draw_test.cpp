// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/play_controls.h"

#include <catch2/catch_test_macros.hpp>

using coney::debug::DebugDrawOptions;
using coney::debug::MeshScope;
using coney::debug::recordedDebugDraw;

namespace {
// Every Debug draw line a run could switch on, with the Entity inspector's selection highlighted.
DebugDrawOptions everythingOn() {
    DebugDrawOptions on;
    on.collision = true;
    on.collisionRadius = 25.0F;
    on.player = true;
    on.groundNormal = true;
    on.camera = true;
    on.places = true;
    on.wallClasses = true;
    on.shapes = true;
    on.strikes = true;
    on.contacts = true;
    on.inspected = 42;
    on.highlight = true;
    on.mesh.wireframe = MeshScope::Everything;
    on.mesh.normals = true;
    return on;
}
} // namespace

TEST_CASE("a recorded frame keeps no debug draw unless --record-debug-draw asks for it", "[debug][clips]") {
    // Expected: docs/guides/testing.md#clips ("Nothing private in the frames"): without the flag every clip is drawn
    // with the Debug draw page's defaults, as before the flag existed, whatever the run switched on.
    const DebugDrawOptions defaults;
    const DebugDrawOptions recorded = recordedDebugDraw(everythingOn(), false);
    CHECK(recorded.collision == defaults.collision);
    CHECK(recorded.collisionRadius == defaults.collisionRadius);
    CHECK(recorded.player == defaults.player);
    CHECK(recorded.camera == defaults.camera);
    CHECK(recorded.shapes == defaults.shapes);
    CHECK(recorded.inspected == defaults.inspected);
    CHECK(recorded.mesh.wireframe == defaults.mesh.wireframe);
    CHECK(recorded.mesh.normals == defaults.mesh.normals);
}

TEST_CASE("a clip made with --record-debug-draw keeps the 3D lines the run switched on and no 2D selection text",
          "[debug][clips]") {
    // Expected: the coordinator's answer on #1123 (docs/guides/running-options.md#record): the scene lines the run
    // switched on stay (the world edits' collision wireframe), while nothing of the 2D debug layer may: the Entity
    // inspector's selection is dropped, as it names itself in the overlay's corner line ("Inspecting: ...").
    const DebugDrawOptions recorded = recordedDebugDraw(everythingOn(), true);
    CHECK(recorded.collision);
    CHECK(recorded.collisionRadius == 25.0F);
    CHECK(recorded.player);
    CHECK(recorded.shapes);
    CHECK(recorded.mesh.wireframe == MeshScope::Everything);
    CHECK(recorded.inspected == 0);
}
