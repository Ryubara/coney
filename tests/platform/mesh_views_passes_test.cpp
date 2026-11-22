// SPDX-License-Identifier: GPL-3.0-or-later
// The Debug draw page's mesh views on a renderer that records its draws (docs/guides/debug-menu.md#mesh-views,
// docs/engine/gpu-renderer.md#scene, #1599): the passes the debug tools' MeshViews gives a recorded model, headless,
// and the GPU renderer's pictures of them (the wireframe, the flat views and their scope, the normals' and bone axes'
// lines) read back from a hidden window, as the GL3 renderer's are. Coney's own debug tooling: the original has no
// such views; the colours and rules are the debug tools' (debug/mesh_views.h, tested in
// tests/debug/mesh_views_test.cpp) and the GPU pass must give the models what the GL3 renderer's passes give them. The
// window cases need SDL's video and a GPU device and skip where there is none.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "support/librw_once.h"
#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "debug/mesh_views.h"
#include "graphics/frame_description.h"
#include "platform/mesh_view_hook.h"
#include "platform/mesh_views.h"
#include "platform/perf_probes.h"
#include "platform/render_engine.h"
#include "support/gpu_capture.h"
#include "support/quad_fixtures.h"

using coney::debug::MeshKind;
using coney::debug::MeshScope;
using coney::debug::MeshShading;
using coney::graphics::MeshId;
using coney::graphics::Rgba;
using coney::platform::DrawKind;
using coney::platform::DrawKindScope;
using coney::platform::MeshViews;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::ViewMaterial;
using coney::platform::ViewPasses;
using coney::test::gpuHere;
using coney::test::near;
using coney::test::pixelAt;
using coney::test::QuadSpec;

namespace {

// The materials of a two-material mesh: "wall" at full alpha and an untextured one at `alpha`.
std::array<ViewMaterial, 2> twoMaterials(std::uint8_t alpha = 255) {
    return {ViewMaterial{"wall", alpha}, ViewMaterial{"", alpha}};
}

} // namespace

TEST_CASE("with every view off a recorded model gets the game's own draw and nothing else",
          "[platform][mesh-views][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: off means untouched: the scene draws every model exactly as
    // without the views.
    coney::debug::DebugDrawOptions options;
    MeshViews views(options);
    for (const DrawKind kind : {DrawKind::Scenery, DrawKind::Humans, DrawKind::Objects}) {
        const ViewPasses passes = views.passesFor(kind, MeshId{1}, twoMaterials());
        CHECK(passes.shaded);
        CHECK(passes.flat.empty());
        CHECK_FALSE(passes.wire.has_value());
    }
}

TEST_CASE("the Material view gives each material of a recorded model the colour its texture and index name",
          "[platform][mesh-views][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *by material*: one colour per material, the same for two models
    // sharing a texture; the flat view takes the model's shaded draw's place.
    coney::debug::DebugDrawOptions options;
    options.mesh.view = MeshShading::Material;
    MeshViews views(options);
    const ViewPasses passes = views.passesFor(DrawKind::Scenery, MeshId{1}, twoMaterials());
    CHECK_FALSE(passes.shaded);
    CHECK_FALSE(passes.wire.has_value());
    REQUIRE(passes.flat.size() == 2);
    CHECK(passes.flat[0] == coney::debug::meshPaletteColour(coney::debug::meshMaterialKey("wall", 0)));
    CHECK(passes.flat[1] == coney::debug::meshPaletteColour(coney::debug::meshMaterialKey("", 1)));
    // The same textures on another model give the same colours.
    const ViewPasses again = views.passesFor(DrawKind::Objects, MeshId{2}, twoMaterials());
    CHECK(again.flat == passes.flat);
}

TEST_CASE("the draw call view counts every material of every flat pass of a frame on from the one before",
          "[platform][mesh-views][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *by draw call*: one colour per draw call of the frame, in the
    // order drawn (the GL3 flat pass takes the next palette colour for each material it draws).
    coney::debug::DebugDrawOptions options;
    options.mesh.view = MeshShading::DrawCall;
    MeshViews views(options);
    const ViewPasses first = views.passesFor(DrawKind::Scenery, MeshId{1}, twoMaterials());
    const ViewPasses second = views.passesFor(DrawKind::Humans, MeshId{2}, twoMaterials());
    REQUIRE(first.flat.size() == 2);
    REQUIRE(second.flat.size() == 2);
    for (std::uint32_t i = 0; i < 2; ++i) {
        CHECK(first.flat[i] == coney::debug::meshPaletteColour(i));
        CHECK(second.flat[i] == coney::debug::meshPaletteColour(2 + i));
    }
    CHECK(views.passDraws() == 4);
}

TEST_CASE("the LOD view colours a model by the alpha its draw fades it to", "[platform][mesh-views][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *by LOD*: green at full detail, yellow while a human fades out
    // with distance (the GL3 flat pass reads the material's alpha).
    coney::debug::DebugDrawOptions options;
    options.mesh.view = MeshShading::Lod;
    MeshViews views(options);
    const ViewPasses full = views.passesFor(DrawKind::Humans, MeshId{1}, twoMaterials(255));
    const ViewPasses fading = views.passesFor(DrawKind::Humans, MeshId{1}, twoMaterials(128));
    REQUIRE(full.flat.size() == 2);
    REQUIRE(fading.flat.size() == 2);
    CHECK(full.flat[0] == coney::debug::meshLodColour(1.0F));
    CHECK(fading.flat[0] == coney::debug::meshLodColour(128.0F / 255.0F));
    CHECK(full.flat[0] != fading.flat[0]);
}

TEST_CASE("a view applies to the models in its scope and the rest stay shaded",
          "[platform][mesh-views][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *Mesh view on*: the selection, all humans or everything; the
    // selection is the Entity inspector's (a human's models, here by mesh id), and *all humans* is the humans and the
    // selection.
    coney::debug::DebugDrawOptions options;
    options.mesh.view = MeshShading::Material;
    MeshViews views(options);
    views.selectMeshes({MeshId{7}});

    options.mesh.viewScope = MeshScope::Selection;
    CHECK_FALSE(views.passesFor(DrawKind::Humans, MeshId{7}, twoMaterials()).shaded);
    CHECK(views.passesFor(DrawKind::Humans, MeshId{8}, twoMaterials()).shaded);
    CHECK(views.passesFor(DrawKind::Scenery, MeshId{9}, twoMaterials()).shaded);

    options.mesh.viewScope = MeshScope::Humans;
    CHECK_FALSE(views.passesFor(DrawKind::Humans, MeshId{8}, twoMaterials()).shaded);
    CHECK_FALSE(views.passesFor(DrawKind::Objects, MeshId{7}, twoMaterials()).shaded); // the selection too
    CHECK(views.passesFor(DrawKind::Objects, MeshId{9}, twoMaterials()).shaded);
    CHECK(views.passesFor(DrawKind::Scenery, MeshId{9}, twoMaterials()).shaded);

    options.mesh.viewScope = MeshScope::Everything;
    CHECK_FALSE(views.passesFor(DrawKind::Scenery, MeshId{9}, twoMaterials()).shaded);
    CHECK_FALSE(views.passesFor(DrawKind::Objects, MeshId{9}, twoMaterials()).shaded);
}

TEST_CASE("the wireframe adds the wire in the colour of the model's kind over its shaded draw",
          "[platform][mesh-views][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: the selection white, all humans green, objects orange and
    // scenery grey (debug::meshWireColour()); the model keeps its shaded draw under the wire.
    coney::debug::DebugDrawOptions options;
    options.mesh.wireframe = MeshScope::Everything;
    MeshViews views(options);
    views.selectMeshes({MeshId{7}});
    const std::array<std::pair<DrawKind, MeshKind>, 3> kinds{{{DrawKind::Scenery, MeshKind::Scenery},
                                                              {DrawKind::Humans, MeshKind::Human},
                                                              {DrawKind::Objects, MeshKind::Object}}};
    for (const auto& [drawKind, meshKind] : kinds) {
        const ViewPasses passes = views.passesFor(drawKind, MeshId{9}, twoMaterials());
        CHECK(passes.shaded);
        CHECK(passes.flat.empty());
        REQUIRE(passes.wire.has_value());
        CHECK(*passes.wire == coney::debug::meshWireColour(meshKind, false));
    }
    const ViewPasses selected = views.passesFor(DrawKind::Humans, MeshId{7}, twoMaterials());
    REQUIRE(selected.wire.has_value());
    CHECK(*selected.wire == coney::debug::meshWireColour(MeshKind::Human, true));

    // Only the selection's: the others get none.
    options.mesh.wireframe = MeshScope::Selection;
    CHECK_FALSE(views.passesFor(DrawKind::Humans, MeshId{9}, twoMaterials()).wire.has_value());
    CHECK(views.passesFor(DrawKind::Humans, MeshId{7}, twoMaterials()).wire.has_value());
}

TEST_CASE("the wireframe-only view gives the wire and no fill, a flat view with the wireframe both",
          "[platform][mesh-views][gpu-renderer]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *wireframe only*: the edges, no fill; the wireframe switch adds
    // the wire over a flat view.
    coney::debug::DebugDrawOptions options;
    options.mesh.view = MeshShading::Wireframe;
    MeshViews views(options);
    const ViewPasses edges = views.passesFor(DrawKind::Scenery, MeshId{1}, twoMaterials());
    CHECK_FALSE(edges.shaded);
    CHECK(edges.flat.empty());
    CHECK(edges.wire.has_value());

    options.mesh.view = MeshShading::Material;
    options.mesh.wireframe = MeshScope::Everything;
    const ViewPasses both = views.passesFor(DrawKind::Scenery, MeshId{1}, twoMaterials());
    CHECK_FALSE(both.shaded);
    CHECK(both.flat.size() == 2);
    CHECK(both.wire.has_value());
}

TEST_CASE("a model of a kind the World page hides gets no pass at all from the recorded path",
          "[platform][mesh-views][gpu-renderer][world-page]") {
    // Expected: docs/guides/debug-menu.md#world: a hidden model gets no pass at all, not even the wireframe (the
    // renderer leaves such a draw out before it asks; the hook agrees when asked).
    coney::debug::DebugDrawOptions options;
    options.mesh.wireframe = MeshScope::Everything;
    coney::graphics::LightingSwitches switches;
    switches.parts.humans = false;
    MeshViews views(options, &switches);
    const ViewPasses hidden = views.passesFor(DrawKind::Humans, MeshId{1}, twoMaterials());
    CHECK_FALSE(hidden.shaded);
    CHECK(hidden.flat.empty());
    CHECK_FALSE(hidden.wire.has_value());
    CHECK(views.passesFor(DrawKind::Scenery, MeshId{1}, twoMaterials()).wire.has_value());
}

namespace {

// A started GPU engine on a 320 x 240 hidden window with a camera of the view 0.5 by 0.375 at a distance of 1, the
// near plane 1, the far 100 and the fog from 50; skips the test where there is no GPU device.
struct GpuFrame {
    std::unique_ptr<RenderEngine> engine;
    std::vector<rw::Atomic*> quads;
    std::vector<MeshId> meshes;

    GpuFrame() {
        if (!gpuHere()) {
            SKIP("no display or no GPU device here");
        }
        coney::platform::WindowDesc desc;
        desc.activate = false;
        desc.hidden = true;
        desc.width = 320;
        desc.height = 240;
        auto started = RenderEngine::start(RenderBackend::Gpu, desc);
        REQUIRE(started.has_value());
        engine = std::move(*started);
        REQUIRE(engine->frameSize().width == 320);
    }
    ~GpuFrame() {
        for (const MeshId mesh : meshes) {
            engine->sceneAssets().removeMesh(mesh);
        }
        for (rw::Atomic* atomic : quads) {
            coney::test::destroyQuad(atomic);
        }
    }
    GpuFrame(const GpuFrame&) = delete;
    GpuFrame& operator=(const GpuFrame&) = delete;
    GpuFrame(GpuFrame&&) = delete;
    GpuFrame& operator=(GpuFrame&&) = delete;

    // A quad of the pixel rectangle `left`..`right` by `top`..`bottom` at distance `z`, added to the scene assets;
    // its index.
    std::size_t addQuad(int left, int right, int top, int bottom, float z, QuadSpec spec) {
        const auto x = [z](int pixel) { return (static_cast<float>(pixel) / 160.0F - 1.0F) * 0.5F * z; };
        const auto y = [z](int pixel) { return (1.0F - static_cast<float>(pixel) / 120.0F) * 0.375F * z; };
        spec.topLeft = {x(left), y(top), z};
        spec.topRight = {x(right), y(top), z};
        spec.bottomLeft = {x(left), y(bottom), z};
        spec.bottomRight = {x(right), y(bottom), z};
        quads.push_back(coney::test::makeQuad(spec));
        auto id = engine->sceneAssets().addAtomic(*quads.back());
        REQUIRE(id.has_value());
        meshes.push_back(*id);
        return meshes.size() - 1;
    }

    // Draws `draws` (quad index, kind, material) through `hook` in one frame and returns the frame's RGBA rows, top
    // first; `lines` are drawn after them.
    struct Item {
        std::size_t quad = 0;
        DrawKind kind = DrawKind::Scenery;
        coney::graphics::Material3d material{};
        std::optional<std::size_t> dual{}; ///< A quad to draw as the dual layer, in the `dualPass`.
        coney::graphics::DualPass dualPass = coney::graphics::DualPass::Layer;
    };
    std::vector<std::uint8_t> draw(coney::platform::MeshViewHook* hook, const std::vector<Item>& items,
                                   std::span<const coney::graphics::SceneLineVertex> lines = {}) {
        // The frame as it is shown, read back as present() ends it.
        std::vector<std::uint8_t> rgba;
        coney::graphics::Extent size;
        engine->setFrameTap([&rgba, &size](std::span<const std::uint8_t> frame, coney::graphics::Extent extent) {
            rgba.assign(frame.begin(), frame.end());
            size = extent;
        });
        engine->beginWindowFrame(Rgba{10, 10, 10, 255});
        coney::graphics::SceneCamera camera;
        camera.halfWidth = 0.5F;
        camera.halfHeight = 0.375F;
        camera.nearClip = 1.0F;
        camera.farClip = 100.0F;
        camera.fogStart = 50.0F;
        engine->setSceneCamera(camera);
        engine->hideKindsOf(hook);
        coney::graphics::SceneStates states;
        states.fogColour = Rgba{0, 200, 0, 255};
        for (const Item& item : items) {
            const DrawKindScope scope(item.kind);
            coney::graphics::Draw3d scene;
            scene.mesh = meshes[item.quad];
            scene.material = item.material;
            scene.states = states;
            if (item.dual) {
                scene.dualLayer = meshes[*item.dual];
                scene.dualPass = item.dualPass;
            }
            engine->drawScene(scene, {});
        }
        engine->hideKindsOf(nullptr);
        if (!lines.empty()) {
            engine->drawSceneLines(lines);
        }
        engine->present();
        engine->setFrameTap({});
        REQUIRE(size.width == 320);
        REQUIRE(size.height == 240);
        return rgba;
    }
};

// Whether any pixel of the column `x` from row `y0` to `y1` (inclusive) is `want` within 2.
bool anyInColumn(const std::vector<std::uint8_t>& rgba, int x, int y0, int y1, Rgba want) {
    for (int y = y0; y <= y1; ++y) {
        if (near(pixelAt(rgba, 320, x, y), want, 2)) {
            return true;
        }
    }
    return false;
}

// Whether any pixel of the row `y` from column `x0` to `x1` (inclusive) is `want` within 2.
bool anyInRow(const std::vector<std::uint8_t>& rgba, int y, int x0, int x1, Rgba want) {
    for (int x = x0; x <= x1; ++x) {
        if (near(pixelAt(rgba, 320, x, y), want, 2)) {
            return true;
        }
    }
    return false;
}

// A prelit, modulated, lit-by-nothing quad in `colour` with a second material.
QuadSpec paintedQuad(Rgba colour) {
    return QuadSpec{.modulate = true, .prelit = true, .prelight = colour, .colour = Rgba{255, 255, 255, 255}};
}

} // namespace

TEST_CASE("the GPU renderer draws the Material view as flat colours that ignore the fog and the texture",
          "[render][renderer][window][scene][mesh-views]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *by material* is flat and unlit, one colour for each material,
    // in place of the shaded draw: a quad at 75 of a fog from 50 to 100 (half fogged shaded) shows its two materials'
    // palette colours exactly, the first triangle the upper left, the second the lower right.
    GpuFrame gpu;
    const std::size_t quad = gpu.addQuad(40, 280, 40, 200, 75.0F,
                                         QuadSpec{.modulate = true,
                                                  .prelit = true,
                                                  .prelight = Rgba{255, 255, 255, 255},
                                                  .colour = Rgba{255, 255, 255, 255},
                                                  .texture = "wall",
                                                  .secondColour = Rgba{255, 0, 0, 255},
                                                  .secondTexture = ""});
    coney::debug::DebugDrawOptions options;
    options.mesh.view = MeshShading::Material;
    MeshViews views(options);
    const std::vector<std::uint8_t> rgba = gpu.draw(&views, {GpuFrame::Item{.quad = quad}});
    CHECK(near(pixelAt(rgba, 320, 100, 80), coney::debug::meshPaletteColour(coney::debug::meshMaterialKey("wall", 0)),
               2));
    CHECK(near(pixelAt(rgba, 320, 220, 160), coney::debug::meshPaletteColour(coney::debug::meshMaterialKey("", 1)), 2));
    // Nothing outside the quad.
    CHECK(near(pixelAt(rgba, 320, 10, 10), Rgba{10, 10, 10, 255}, 1));
}

TEST_CASE("the GPU renderer draws the draw call view's colours in the order the models are drawn",
          "[render][renderer][window][scene][mesh-views]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *by draw call*: the palette's colours in the order drawn, one
    // for each material: two two-material quads drawn left then right take palette 0 and 1, then 2 and 3.
    GpuFrame gpu;
    const QuadSpec spec{.modulate = true, .prelit = true, .secondColour = Rgba{255, 255, 255, 255}};
    const std::size_t left = gpu.addQuad(20, 140, 60, 180, 10.0F, spec);
    const std::size_t right = gpu.addQuad(180, 300, 60, 180, 10.0F, spec);
    coney::debug::DebugDrawOptions options;
    options.mesh.view = MeshShading::DrawCall;
    MeshViews views(options);
    const std::vector<std::uint8_t> rgba =
        gpu.draw(&views, {GpuFrame::Item{.quad = left}, GpuFrame::Item{.quad = right}});
    // The upper-left triangle of each is its first material, the lower right its second.
    CHECK(near(pixelAt(rgba, 320, 50, 90), coney::debug::meshPaletteColour(0), 2));
    CHECK(near(pixelAt(rgba, 320, 110, 150), coney::debug::meshPaletteColour(1), 2));
    CHECK(near(pixelAt(rgba, 320, 210, 90), coney::debug::meshPaletteColour(2), 2));
    CHECK(near(pixelAt(rgba, 320, 270, 150), coney::debug::meshPaletteColour(3), 2));
}

TEST_CASE("the GPU renderer draws the LOD view green at full detail and yellow while a human fades",
          "[render][renderer][window][scene][mesh-views]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *by LOD*: green at full detail, yellow while a human fades out
    // with distance; the fade is the alpha the draw gives his materials (MaterialColour::Fade).
    GpuFrame gpu;
    const QuadSpec spec{.modulate = true, .prelit = true};
    const std::size_t full = gpu.addQuad(20, 140, 60, 180, 10.0F, spec);
    const std::size_t fading = gpu.addQuad(180, 300, 60, 180, 10.0F, spec);
    coney::debug::DebugDrawOptions options;
    options.mesh.view = MeshShading::Lod;
    MeshViews views(options);
    coney::graphics::Material3d faded;
    faded.colour = coney::graphics::MaterialColour::Fade;
    faded.fade = 100;
    const std::vector<std::uint8_t> rgba =
        gpu.draw(&views, {GpuFrame::Item{.quad = full, .kind = DrawKind::Humans},
                          GpuFrame::Item{.quad = fading, .kind = DrawKind::Humans, .material = faded}});
    CHECK(near(pixelAt(rgba, 320, 50, 90), coney::debug::meshLodColour(1.0F), 2));
    CHECK(near(pixelAt(rgba, 320, 210, 90), coney::debug::meshLodColour(100.0F / 255.0F), 2));
}

TEST_CASE("the GPU renderer draws the wireframe over the shaded model, on its edges",
          "[render][renderer][window][scene][mesh-views]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *Model wireframe*: each model's own triangles as lines over the
    // shaded scene, scenery grey: the fill stays what the shaded draw made it, and the quad's outline is the wire
    // colour.
    GpuFrame gpu;
    const std::size_t quad = gpu.addQuad(80, 240, 60, 180, 10.0F, paintedQuad(Rgba{200, 0, 0, 255}));
    coney::debug::DebugDrawOptions options;
    options.mesh.wireframe = MeshScope::Everything;
    MeshViews views(options);
    const std::vector<std::uint8_t> rgba = gpu.draw(&views, {GpuFrame::Item{.quad = quad}});
    const Rgba wire = coney::debug::meshWireColour(MeshKind::Scenery, false);
    CHECK(near(pixelAt(rgba, 320, 120, 100), Rgba{200, 0, 0, 255}, 2)); // the fill, inside a triangle
    CHECK(anyInRow(rgba, 120, 77, 83, wire));                           // the left edge
    CHECK(anyInRow(rgba, 120, 237, 243, wire));                         // the right edge
    CHECK(anyInColumn(rgba, 160, 57, 63, wire));                        // the top edge
}

TEST_CASE("the GPU renderer draws a human's blood layer over his wireframe, as GL3 does",
          "[render][renderer][window][scene][mesh-views]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: on GL3 the blood layer is drawn after the body's passes and
    // covers the lines where it blends; here an opaque layer over the same quad leaves no wire colour inside the
    // quad's outline (the diagonal between its two triangles is the line it must cover), where a layer drawn before
    // the wire leaves the diagonal showing.
    GpuFrame gpu;
    const std::size_t body = gpu.addQuad(80, 240, 60, 180, 10.0F, paintedQuad(Rgba{200, 0, 0, 255}));
    const std::size_t blood = gpu.addQuad(80, 240, 60, 180, 10.0F, paintedQuad(Rgba{0, 0, 200, 255}));
    coney::debug::DebugDrawOptions options;
    options.mesh.wireframe = MeshScope::Everything;
    MeshViews views(options);
    const Rgba wire = coney::debug::meshWireColour(MeshKind::Scenery, false);
    const std::vector<std::uint8_t> rgba =
        gpu.draw(&views, {GpuFrame::Item{.quad = body, .dual = blood, .dualPass = coney::graphics::DualPass::Blood}});
    // The diagonal runs from the top right corner to the bottom left of the quad: no pixel inside the outline is wire.
    bool wireInside = false;
    for (int y = 66; y <= 174 && !wireInside; ++y) {
        wireInside = anyInRow(rgba, y, 86, 234, wire);
    }
    CHECK_FALSE(wireInside);
}

TEST_CASE("the GPU renderer draws the wireframe-only view as edges with no fill",
          "[render][renderer][window][scene][mesh-views]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *wireframe only*: the edges, no fill: inside the quad the clear
    // colour shows through.
    GpuFrame gpu;
    const std::size_t quad = gpu.addQuad(80, 240, 60, 180, 10.0F, paintedQuad(Rgba{200, 0, 0, 255}));
    coney::debug::DebugDrawOptions options;
    options.mesh.view = MeshShading::Wireframe;
    MeshViews views(options);
    const std::vector<std::uint8_t> rgba = gpu.draw(&views, {GpuFrame::Item{.quad = quad}});
    CHECK(near(pixelAt(rgba, 320, 120, 100), Rgba{10, 10, 10, 255}, 1));
    CHECK(anyInRow(rgba, 120, 77, 83, coney::debug::meshWireColour(MeshKind::Scenery, false)));
}

TEST_CASE("the GPU renderer applies a view to the selection's models alone",
          "[render][renderer][window][scene][mesh-views]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: *Mesh view on: selection* flattens the Entity inspector's
    // selection and leaves the others shaded.
    GpuFrame gpu;
    const std::size_t selected = gpu.addQuad(20, 140, 60, 180, 10.0F, paintedQuad(Rgba{200, 0, 0, 255}));
    const std::size_t other = gpu.addQuad(180, 300, 60, 180, 10.0F, paintedQuad(Rgba{200, 0, 0, 255}));
    coney::debug::DebugDrawOptions options;
    options.mesh.view = MeshShading::DrawCall;
    options.mesh.viewScope = MeshScope::Selection;
    MeshViews views(options);
    views.selectMeshes({gpu.meshes[selected]});
    const std::vector<std::uint8_t> rgba = gpu.draw(&views, {GpuFrame::Item{.quad = selected, .kind = DrawKind::Humans},
                                                             GpuFrame::Item{.quad = other, .kind = DrawKind::Humans}});
    CHECK(near(pixelAt(rgba, 320, 50, 90), coney::debug::meshPaletteColour(0), 2));
    CHECK(near(pixelAt(rgba, 320, 210, 90), Rgba{200, 0, 0, 255}, 2));
}

TEST_CASE("the GPU renderer draws scene lines in their colours, behind what is nearer",
          "[render][renderer][window][scene][mesh-views]") {
    // Expected: docs/guides/debug-menu.md#mesh-views: the normals and bone axes are Debug draw lines, tested against
    // the depth the scene wrote: a line behind a quad is hidden by it, one in front of it is seen.
    GpuFrame gpu;
    const std::size_t quad = gpu.addQuad(100, 220, 60, 180, 10.0F, paintedQuad(Rgba{0, 200, 0, 255}));
    const auto at = [](float px, float py, float z) {
        return coney::graphics::SceneLineVertex{{(px / 160.0F - 1.0F) * 0.5F * z, (1.0F - py / 120.0F) * 0.375F * z, z},
                                                Rgba{255, 0, 0, 255}};
    };
    // Two lines across the middle: one behind the quad (z 20), one in front of it (z 5, lower down).
    const std::array<coney::graphics::SceneLineVertex, 4> lines{at(20.0F, 90.5F, 20.0F), at(300.0F, 90.5F, 20.0F),
                                                                at(20.0F, 150.5F, 5.0F), at(300.0F, 150.5F, 5.0F)};
    coney::debug::DebugDrawOptions options;
    MeshViews views(options);
    const std::vector<std::uint8_t> rgba = gpu.draw(&views, {GpuFrame::Item{.quad = quad}}, lines);
    // Beside the quad both are seen in the line's colour; over it the near one stays and the far one is covered.
    CHECK(anyInColumn(rgba, 60, 88, 93, Rgba{255, 0, 0, 255}));
    CHECK(anyInColumn(rgba, 60, 148, 153, Rgba{255, 0, 0, 255}));
    CHECK(anyInColumn(rgba, 160, 148, 153, Rgba{255, 0, 0, 255}));
    CHECK_FALSE(anyInColumn(rgba, 160, 88, 93, Rgba{255, 0, 0, 255}));
    CHECK(near(pixelAt(rgba, 320, 160, 70), Rgba{0, 200, 0, 255}, 2));
}
