// SPDX-License-Identifier: GPL-3.0-or-later
// The mesh views' passes in the frame description, headless (docs/guides/debug-menu.md#mesh-views, #1599): what a Flat
// or Wire pass of a recorded 3D draw is coloured, lit and stated with, and the colours, lines and passes the frame
// description keeps. They are Coney's own debug tooling and have no counterpart in the original; what the GPU renderer
// must match is the GL3 renderer's flat and wire passes (platform/mesh_views.cpp: the material colours swapped for the
// view's opaque colours and the textures for none, one white ambient light, the fog off, the alpha test always, the
// wire in line mode with depth writes off), so the expected values are that pass's.
#include <array>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "graphics/frame_description.h"
#include "graphics/mesh_data.h"
#include "graphics/scene_batch.h"

using coney::graphics::AlphaTest;
using coney::graphics::Draw3d;
using coney::graphics::FrameDescription;
using coney::graphics::FrameStart;
using coney::graphics::MeshData;
using coney::graphics::MeshMaterial;
using coney::graphics::Rgba;
using coney::graphics::SceneLineVertex;
using coney::graphics::ScenePass;
using coney::graphics::SceneStates;
using coney::graphics::SubMesh;
using coney::graphics::ViewPass;

namespace {

// A mesh of three triangles, one for each of three materials: translucent and prelit-less, untextured or named.
MeshData threeMaterialMesh() {
    MeshData mesh;
    mesh.vertices.resize(9);
    mesh.indices = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    mesh.subMeshes = {SubMesh{0, 3, 0}, SubMesh{3, 3, 1}, SubMesh{6, 3, 2}};
    mesh.materials = {MeshMaterial{.colour = Rgba{10, 20, 30, 255}, .texture = "wall"},
                      MeshMaterial{.colour = Rgba{40, 50, 60, 128}, .texture = ""},
                      MeshMaterial{.colour = Rgba{70, 80, 90, 0}, .texture = "floor"}};
    return mesh;
}

// The first scene pass of `frame`; fails the test when there is none.
const ScenePass& sceneOf(const FrameDescription& frame) {
    const ScenePass* found = nullptr;
    for (const auto& pass : frame.passes()) {
        if (const auto* scene = std::get_if<ScenePass>(&pass); scene != nullptr) {
            found = scene;
            break;
        }
    }
    REQUIRE(found != nullptr);
    return *found;
}

} // namespace

TEST_CASE("a mesh views' flat pass draws each material in its own opaque colour and modulates by it",
          "[graphics][mesh-views][gpu-renderer][scene]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: the flat views swap every material's colour for the view's
    // opaque one (rwColour() makes its alpha 255) and light the mesh with white alone, so the mesh shows exactly the
    // colour whether or not it modulates by its own materials.
    const MeshData mesh = threeMaterialMesh();
    const std::array<Rgba, 3> given{Rgba{255, 0, 0, 255}, Rgba{0, 255, 0, 17}, Rgba{0, 0, 255, 255}};
    const coney::graphics::DrawColours colours = coney::graphics::viewColoursOf(mesh, given);
    REQUIRE(colours.colours.size() == 3);
    CHECK(colours.colours[0] == Rgba{255, 0, 0, 255});
    CHECK(colours.colours[1] == Rgba{0, 255, 0, 255}); // opaque whatever the view's alpha
    CHECK(colours.colours[2] == Rgba{0, 0, 255, 255});
    CHECK(colours.modulate);
    CHECK_FALSE(mesh.modulate); // the mesh itself does not: the pass does
}

TEST_CASE("a mesh views' wire pass takes one colour for every material, and white where none is given",
          "[graphics][mesh-views][gpu-renderer][scene]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: the wire pass draws every material in the plan's one wire colour
    // (flatPass(lines: true) gives each material plan.wireColour).
    const MeshData mesh = threeMaterialMesh();
    const std::array<Rgba, 1> wire{Rgba{80, 160, 255, 255}};
    const coney::graphics::DrawColours one = coney::graphics::viewColoursOf(mesh, wire);
    REQUIRE(one.colours.size() == 3);
    for (const Rgba& colour : one.colours) {
        CHECK(colour == Rgba{80, 160, 255, 255});
    }
    const coney::graphics::DrawColours none = coney::graphics::viewColoursOf(mesh, {});
    REQUIRE(none.colours.size() == 3);
    for (const Rgba& colour : none.colours) {
        CHECK(colour == Rgba{255, 255, 255, 255});
    }
}

TEST_CASE("a mesh views' pass is lit by one white ambient light and nothing else",
          "[graphics][mesh-views][gpu-renderer][scene]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: "lights it with one white ambient light" (flatWorld()'s one
    // ambient light, white), so a vertex is white before the material's colour modulates it, with no directional,
    // point or spot light.
    const coney::graphics::DrawLights lit = coney::graphics::viewLightsOf();
    CHECK(lit.ambient == std::array<float, 3>{1.0F, 1.0F, 1.0F});
    CHECK(lit.count == 0);
}

TEST_CASE("a mesh views' pass is not fogged, keeps every fragment, and a wire pass writes no depth",
          "[graphics][mesh-views][gpu-renderer][scene]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: flatPass() sets FOGENABLE 0 and ALPHATESTFUNC ALWAYS for both
    // passes and ZWRITEENABLE 0 for the lines; the draw's own depth test, culling and blending stay.
    SceneStates drawn;
    drawn.depthTest = true;
    drawn.depthWrite = true;
    drawn.cull = coney::graphics::CullFaces::Front;
    drawn.fog = true;
    drawn.alphaBlend = true;
    drawn.alphaTest = AlphaTest::GreaterEqual;

    const SceneStates flat = coney::graphics::viewStatesOf(drawn, ViewPass::Flat);
    CHECK_FALSE(flat.fog);
    CHECK(flat.alphaTest == AlphaTest::Always);
    CHECK(flat.depthWrite);
    CHECK(flat.depthTest);
    CHECK(flat.cull == coney::graphics::CullFaces::Front);
    CHECK(flat.alphaBlend);

    const SceneStates wire = coney::graphics::viewStatesOf(drawn, ViewPass::Wire);
    CHECK_FALSE(wire.fog);
    CHECK(wire.alphaTest == AlphaTest::Always);
    CHECK_FALSE(wire.depthWrite);
    CHECK(wire.depthTest);
    CHECK(wire.cull == coney::graphics::CullFaces::Front);

    // The game's own pass is left as it is.
    CHECK(coney::graphics::viewStatesOf(drawn, ViewPass::Shaded) == drawn);
}

TEST_CASE("the frame description keeps the colours of a mesh view's pass beside its draw",
          "[graphics][mesh-views][frame-description]") {
    // Expected: docs/engine/renderer.md#frame-description: a draw's ranges point into the frame's arrays, as the
    // lights', textures' and bones' do; a shaded draw has none.
    FrameDescription frame;
    frame.begin(FrameStart{});
    Draw3d shaded;
    frame.addSceneDraw(shaded, {}, {}, {});
    Draw3d flat;
    flat.view = ViewPass::Flat;
    const std::array<Rgba, 2> flatColours{Rgba{1, 2, 3, 255}, Rgba{4, 5, 6, 255}};
    frame.addSceneDraw(flat, {}, {}, {}, {}, flatColours);
    Draw3d wire;
    wire.view = ViewPass::Wire;
    const std::array<Rgba, 1> wireColour{Rgba{7, 8, 9, 255}};
    frame.addSceneDraw(wire, {}, {}, {}, {}, wireColour);

    const ScenePass& scene = sceneOf(frame);
    REQUIRE(scene.draws.size() == 3);
    CHECK(frame.viewColoursOf(scene.draws[0]).empty());
    CHECK(scene.draws[1].view == ViewPass::Flat);
    const std::span<const Rgba> first = frame.viewColoursOf(scene.draws[1]);
    REQUIRE(first.size() == 2);
    CHECK(first[0] == Rgba{1, 2, 3, 255});
    CHECK(first[1] == Rgba{4, 5, 6, 255});
    CHECK(scene.draws[2].view == ViewPass::Wire);
    const std::span<const Rgba> second = frame.viewColoursOf(scene.draws[2]);
    REQUIRE(second.size() == 1);
    CHECK(second[0] == Rgba{7, 8, 9, 255});

    // A new frame forgets them.
    frame.begin(FrameStart{});
    Draw3d again;
    again.view = ViewPass::Wire;
    frame.addSceneDraw(again, {}, {}, {}, {}, wireColour);
    CHECK(frame.viewColoursOf(sceneOf(frame).draws[0]).size() == 1);
}

TEST_CASE("the frame description keeps scene lines two vertices to a line, in the scene pass they join",
          "[graphics][mesh-views][frame-description]") {
    // Expected: docs/engine/renderer.md#frame-description: lines join the scene pass the last pass is, or start one
    // that keeps the camera set last; a vertex with no partner is dropped, and nothing is added for no whole line.
    FrameDescription frame;
    frame.begin(FrameStart{});
    frame.addSceneDraw(Draw3d{}, {}, {}, {});
    const std::array<SceneLineVertex, 5> vertices{SceneLineVertex{{0.0F, 0.0F, 0.0F}, Rgba{255, 0, 0, 255}},
                                                  SceneLineVertex{{1.0F, 0.0F, 0.0F}, Rgba{255, 0, 0, 255}},
                                                  SceneLineVertex{{0.0F, 1.0F, 0.0F}, Rgba{0, 255, 0, 255}},
                                                  SceneLineVertex{{0.0F, 2.0F, 0.0F}, Rgba{0, 255, 0, 255}},
                                                  SceneLineVertex{{9.0F, 9.0F, 9.0F}, Rgba{1, 1, 1, 255}}};
    frame.addSceneLines(vertices);
    REQUIRE(frame.passes().size() == 1);
    const ScenePass& scene = sceneOf(frame);
    CHECK(scene.draws.size() == 1);
    REQUIRE(scene.lines.size() == 4); // the fifth had no partner
    CHECK(scene.lines[0] == vertices[0]);
    CHECK(scene.lines[3] == vertices[3]);

    // No whole line: nothing.
    frame.addSceneLines(std::span<const SceneLineVertex>(vertices).first(1));
    CHECK(sceneOf(frame).lines.size() == 4);
    frame.addSceneLines({});
    CHECK(sceneOf(frame).lines.size() == 4);

    // With no scene pass yet, a new one holds them; the next frame holds none.
    frame.begin(FrameStart{});
    frame.addSceneLines(std::span<const SceneLineVertex>(vertices).first(2));
    CHECK(sceneOf(frame).draws.empty());
    CHECK(sceneOf(frame).lines.size() == 2);
    frame.begin(FrameStart{});
    CHECK(frame.passes().empty());
}
