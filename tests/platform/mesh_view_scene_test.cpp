// SPDX-License-Identifier: GPL-3.0-or-later
// The mesh views on a renderer that records its draws (docs/guides/debug-menu.md#mesh-views,
// docs/engine/gpu-renderer.md #scene, #1599): the passes the hook gives a recorded model, and the lines, land in the
// frame description as the GPU renderer reads them. The NULL backend records as the GPU backend does, from the same
// call, with no window. They are Coney's own debug tooling; the passes' colours and rules are the debug tools'
// (tests/platform/mesh_views_test.cpp).
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

#include "support/librw_once.h"
#include <catch2/catch_test_macros.hpp>

#include "graphics/frame_description.h"
#include "platform/mesh_view_hook.h"
#include "platform/perf_probes.h"
#include "platform/render_engine.h"
#include "support/mesh_view_pass_hook.h"
#include "support/quad_fixtures.h"

using coney::graphics::Draw3d;
using coney::graphics::Rgba;
using coney::graphics::ScenePass;
using coney::graphics::ViewPass;
using coney::platform::DrawKind;
using coney::platform::DrawKindScope;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::ViewPasses;
using coney::test::PassHook;

namespace {

// The frame's one scene pass; fails the test when it has none.
const ScenePass& sceneOf(const coney::graphics::FrameDescription& frame) {
    for (const auto& pass : frame.passes()) {
        if (const auto* scene = std::get_if<ScenePass>(&pass); scene != nullptr) {
            return *scene;
        }
    }
    FAIL("the frame has no scene pass");
    return std::get<ScenePass>(frame.passes().front());
}

// A NULL-backend engine with a quad of two materials in its scene assets: the first textured "wall" in white, the
// second untextured and red.
struct Fixture {
    std::unique_ptr<RenderEngine> engine;
    rw::Atomic* quad = nullptr;
    coney::graphics::MeshId mesh;

    Fixture() {
        auto started = RenderEngine::start(RenderBackend::Null, {});
        REQUIRE(started.has_value());
        engine = std::move(*started);
        quad = coney::test::makeQuad(
            coney::test::QuadSpec{.texture = "wall", .secondColour = Rgba{255, 0, 0, 255}, .secondTexture = ""});
        auto id = engine->sceneAssets().addAtomic(*quad);
        REQUIRE(id.has_value());
        mesh = *id;
    }
    ~Fixture() {
        engine->sceneAssets().removeMesh(mesh);
        coney::test::destroyQuad(quad);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;

    // Draws the quad once as `kind` with `hook` offered, in a frame of its own, and returns the draws the frame holds.
    std::vector<Draw3d> record(PassHook* hook, DrawKind kind, coney::graphics::Material3d material = {},
                               std::span<const coney::graphics::Transform3d> bones = {}) {
        engine->beginWindowFrame(Rgba{0, 0, 0, 255});
        engine->setSceneCamera(coney::graphics::SceneCamera{});
        engine->hideKindsOf(hook);
        {
            const DrawKindScope scope(kind);
            Draw3d draw;
            draw.mesh = mesh;
            draw.material = material;
            engine->drawScene(draw, {}, bones);
        }
        engine->hideKindsOf(nullptr);
        std::vector<Draw3d> draws;
        for (const auto& pass : engine->frame().passes()) {
            if (const auto* scene = std::get_if<ScenePass>(&pass); scene != nullptr) {
                draws.insert(draws.end(), scene->draws.begin(), scene->draws.end());
            }
        }
        engine->present(); // ends the frame, so the next one can begin; its description stays until then
        lastFrame = &engine->frame();
        return draws;
    }

    const coney::graphics::FrameDescription* lastFrame = nullptr;
};

} // namespace

TEST_CASE("a recorded model's flat view is recorded in place of its shaded draw, in the colours the hook gives",
          "[platform][mesh-views][frame-description][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: a flat view applies to a model instead of its shaded draw, one
    // colour for each material; on a renderer that records its draws that is one recorded draw of the same mesh in the
    // Flat pass with the colours beside it.
    Fixture fixture;
    PassHook hook(ViewPasses{.shaded = false, .flat = {Rgba{255, 0, 0, 255}, Rgba{0, 0, 255, 255}}, .wire = {}});
    const std::vector<Draw3d> draws = fixture.record(&hook, DrawKind::Humans);
    REQUIRE(draws.size() == 1);
    CHECK(draws[0].view == ViewPass::Flat);
    CHECK(draws[0].mesh == fixture.mesh);
    const auto colours = fixture.lastFrame->viewColoursOf(draws[0]);
    REQUIRE(colours.size() == 2);
    CHECK(colours[0] == Rgba{255, 0, 0, 255});
    CHECK(colours[1] == Rgba{0, 0, 255, 255});
}

TEST_CASE("a wireframe over the shaded model is a second recorded draw in the Wire pass",
          "[platform][mesh-views][frame-description][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: the wireframe is drawn over the shaded scene: the game's own
    // draw first, then the wire in its one colour.
    Fixture fixture;
    PassHook hook(ViewPasses{.shaded = true, .flat = {}, .wire = Rgba{0, 255, 0, 255}});
    const std::vector<Draw3d> draws = fixture.record(&hook, DrawKind::Scenery);
    REQUIRE(draws.size() == 2);
    CHECK(draws[0].view == ViewPass::Shaded);
    CHECK(draws[1].view == ViewPass::Wire);
    CHECK(draws[1].mesh == fixture.mesh);
    const auto colour = fixture.lastFrame->viewColoursOf(draws[1]);
    REQUIRE(colour.size() == 1);
    CHECK(colour[0] == Rgba{0, 255, 0, 255});
    CHECK(fixture.lastFrame->viewColoursOf(draws[0]).empty());
}

TEST_CASE("a wireframe-only view records the wire alone, and a flat view with a wireframe records both",
          "[platform][mesh-views][frame-description][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *wireframe only* draws the edges with no fill; the wireframe
    // switch adds the wire over a flat view too.
    Fixture fixture;
    PassHook edges(ViewPasses{.shaded = false, .flat = {}, .wire = Rgba{255, 255, 255, 255}});
    const std::vector<Draw3d> wireOnly = fixture.record(&edges, DrawKind::Objects);
    REQUIRE(wireOnly.size() == 1);
    CHECK(wireOnly[0].view == ViewPass::Wire);

    PassHook both(
        ViewPasses{.shaded = false, .flat = {Rgba{1, 2, 3, 255}, Rgba{4, 5, 6, 255}}, .wire = Rgba{9, 9, 9, 255}});
    const std::vector<Draw3d> flatAndWire = fixture.record(&both, DrawKind::Objects);
    REQUIRE(flatAndWire.size() == 2);
    CHECK(flatAndWire[0].view == ViewPass::Flat);
    CHECK(flatAndWire[1].view == ViewPass::Wire);
}

TEST_CASE("a hook that gives the game's own draw records it as it is, and no hook asks nothing",
          "[platform][mesh-views][frame-description][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: off means untouched: with every view off the scene draws every
    // model exactly as without them.
    Fixture fixture;
    PassHook own;
    const std::vector<Draw3d> draws = fixture.record(&own, DrawKind::Humans);
    REQUIRE(draws.size() == 1);
    CHECK(draws[0].view == ViewPass::Shaded);
    CHECK(fixture.lastFrame->viewColoursOf(draws[0]).empty());
    CHECK(own.asked.size() == 1);

    const std::vector<Draw3d> none = fixture.record(nullptr, DrawKind::Humans);
    REQUIRE(none.size() == 1);
    CHECK(none[0].view == ViewPass::Shaded);
}

TEST_CASE("the hook is asked the kind, the mesh and each material's texture and alpha of a recorded model",
          "[platform][mesh-views][frame-description][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: the Material view colours by the material's texture name and
    // index, the LOD view by the alpha the draw gives it (a human's distance fade); the kind and the mesh pick the
    // scope and the selection.
    Fixture fixture;
    PassHook hook;
    coney::graphics::Material3d faded;
    faded.colour = coney::graphics::MaterialColour::Fade;
    faded.fade = 100;
    static_cast<void>(fixture.record(&hook, DrawKind::Humans, faded));
    REQUIRE(hook.asked.size() == 1);
    const coney::test::PassQuestion& question = hook.asked[0];
    CHECK(question.kind == DrawKind::Humans);
    CHECK(question.mesh == fixture.mesh);
    REQUIRE(question.textures.size() == 2);
    CHECK(question.textures[0] == "wall");
    CHECK(question.textures[1].empty());
    REQUIRE(question.alphas.size() == 2);
    CHECK(question.alphas[0] == 100);
    CHECK(question.alphas[1] == 100);
}

TEST_CASE("a model of a kind the hook hides is neither recorded nor asked about",
          "[platform][mesh-views][frame-description][gpu-renderer][world-page]") {
    // Expected: docs/guides/debug-menu.md#world: a hidden model gets no pass at all, not even the wireframe.
    Fixture fixture;
    PassHook hook(ViewPasses{.shaded = true, .flat = {}, .wire = Rgba{1, 1, 1, 255}}, {DrawKind::Humans});
    CHECK(fixture.record(&hook, DrawKind::Humans).empty());
    CHECK(hook.asked.empty());
    CHECK(fixture.record(&hook, DrawKind::Scenery).size() == 2);
}

TEST_CASE("a view's pass of a posed model carries his pose and no second layer",
          "[platform][mesh-views][frame-description][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: whatever skins the shaded pass skins the views' passes the same
    // way, and a human's blood layer is drawn only with the shaded draw.
    Fixture fixture;
    PassHook hook(
        ViewPasses{.shaded = true, .flat = {Rgba{1, 1, 1, 255}, Rgba{2, 2, 2, 255}}, .wire = Rgba{3, 3, 3, 255}});
    const std::array<coney::graphics::Transform3d, 2> pose{};
    fixture.engine->beginWindowFrame(Rgba{0, 0, 0, 255});
    fixture.engine->setSceneCamera(coney::graphics::SceneCamera{});
    fixture.engine->hideKindsOf(&hook);
    {
        const DrawKindScope scope(DrawKind::Humans);
        Draw3d draw;
        draw.mesh = fixture.mesh;
        draw.dualLayer = fixture.mesh; // stands in for the blood layer
        fixture.engine->drawScene(draw, {}, pose);
    }
    fixture.engine->hideKindsOf(nullptr);
    const ScenePass& scene = sceneOf(fixture.engine->frame());
    REQUIRE(scene.draws.size() == 3);
    CHECK(scene.draws[0].view == ViewPass::Shaded);
    CHECK(scene.draws[0].dualLayer == fixture.mesh);
    for (const std::size_t i : {std::size_t{1}, std::size_t{2}}) {
        CHECK_FALSE(scene.draws[i].dualLayer.valid());
        CHECK(scene.draws[i].boneCount == 2);
    }
}

TEST_CASE("scene lines are recorded into the frame description for the renderer that draws it",
          "[platform][mesh-views][frame-description][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: the normals and bone axes are Debug draw lines: two vertices
    // to a line, in the scene pass the frame's 3D draws are in.
    Fixture fixture;
    fixture.engine->beginWindowFrame(Rgba{0, 0, 0, 255});
    fixture.engine->setSceneCamera(coney::graphics::SceneCamera{});
    Draw3d draw;
    draw.mesh = fixture.mesh;
    fixture.engine->drawScene(draw, {});
    const std::array<coney::graphics::SceneLineVertex, 2> line{
        coney::graphics::SceneLineVertex{{0.0F, 0.0F, 1.0F}, Rgba{255, 0, 0, 255}},
        coney::graphics::SceneLineVertex{{0.0F, 1.0F, 1.0F}, Rgba{255, 0, 0, 255}}};
    fixture.engine->drawSceneLines(line);
    const ScenePass& scene = sceneOf(fixture.engine->frame());
    CHECK(scene.draws.size() == 1);
    REQUIRE(scene.lines.size() == 2);
    CHECK(scene.lines[0] == line[0]);
    CHECK(scene.lines[1] == line[1]);
}
