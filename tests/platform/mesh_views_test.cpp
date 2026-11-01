// SPDX-License-Identifier: GPL-3.0-or-later
// The mesh views' platform half (docs/guides/debug-menu.md#mesh-views) on librw's NULL device, with no window or GPU:
// the Mesh group's counts of a synthetic clump against the librw geometry the play mode draws it with, the pass that
// leaves a model as the game draws it while the views are off and puts every material back after a flat pass, and the
// scene counters by kind.
#include <array>
#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "characters/character_model.h"
#include "core/error.h"
#include "debug/debug_session.h"
#include "debug/entity_inspect.h"
#include "debug/mesh_stats.h"
#include "debug/mesh_views.h"
#include "debug/play_controls.h"
#include "debug/tunables.h"
#include "graphics/level_lighting.h"
#include "platform/character_mesh.h"
#include "platform/draw_calls.h"
#include "platform/mesh_view_hook.h"
#include "platform/mesh_views.h"
#include "platform/perf_probes.h"
#include "platform/render_engine.h"
#include "platform/scene_lighting.h"
#include "support/character_fixtures.h"
#include "world/view_frustum.h"

namespace {

// The synthetic clump of the character fixtures (support/character_fixtures.h), decoded.
coney::characters::CharacterModel fixtureModel() {
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    return std::move(*model);
}

// Each material's colour and texture, to compare before and after a pass.
struct MaterialLook {
    rw::RGBA colour;
    rw::Texture* texture = nullptr;
    float ambient = 0.0F;

    friend bool operator==(const MaterialLook& a, const MaterialLook& b) {
        return a.colour.red == b.colour.red && a.colour.green == b.colour.green && a.colour.blue == b.colour.blue &&
               a.colour.alpha == b.colour.alpha && a.texture == b.texture && a.ambient == b.ambient;
    }
};

// The materials of `atomic` as they are now.
std::vector<MaterialLook> looksOf(const rw::Atomic* atomic) {
    std::vector<MaterialLook> looks;
    const rw::Geometry* geometry = atomic->geometry;
    for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
        const rw::Material* material = geometry->matList.materials[i];
        looks.push_back(MaterialLook{
            .colour = material->color, .texture = material->texture, .ambient = material->surfaceProps.ambient});
    }
    return looks;
}

} // namespace

TEST_CASE("the Mesh group counts a synthetic clump as librw builds the geometry the play mode draws", "[mesh]") {
    // Expected: unverified, #210 (Coney's own renderer: the counts must equal librw's geometry)
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const coney::characters::CharacterModel model = fixtureModel();
    {
        const coney::platform::CharacterMesh mesh(model, nullptr);
        const rw::Geometry* geometry = mesh.atomic()->geometry;
        const coney::debug::MeshStats stats = coney::debug::characterMeshStats(model, {});
        CHECK(stats.triangles == static_cast<std::size_t>(geometry->numTriangles));
        CHECK(stats.vertices == static_cast<std::size_t>(geometry->numVertices));
        CHECK(stats.materials.size() == static_cast<std::size_t>(geometry->matList.numMaterials));
        // One draw per librw mesh: buildMeshes() makes one for every material.
        REQUIRE(geometry->meshHeader != nullptr);
        CHECK(stats.drawsPerPass == static_cast<std::size_t>(geometry->meshHeader->numMeshes));
        // The fixture: four distinct vertices, two triangles, three vertices with one weight and one with two.
        CHECK(stats.triangles == 2);
        CHECK(stats.vertices == 4);
        CHECK(stats.verticesByWeights == std::array<std::size_t, 5>{0, 3, 1, 0, 0});
        CHECK(stats.hierarchyBones == model.clump.hierarchy.size());
        // Nothing drawn yet.
        CHECK(mesh.drawnPasses() == 0);
    }
}

TEST_CASE("the mesh views draw a model as the game does while off and put its materials back after a pass", "[mesh]") {
    // Expected: unverified, #210 (faithful first: with every view off the game's draw alone)
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const coney::characters::CharacterModel model = fixtureModel();
    {
        coney::platform::CharacterMesh mesh(model, nullptr);
        coney::debug::DebugDrawOptions options;
        coney::platform::MeshViews views(options);
        const std::vector<MaterialLook> before = looksOf(mesh.atomic());
        int shadedDraws = 0;
        const auto shaded = [&shadedDraws] { ++shadedDraws; };
        // Off: the shaded draw once, nothing else.
        views.draw(mesh.atomic(), coney::platform::DrawKind::Humans, false, shaded);
        CHECK(shadedDraws == 1);
        CHECK(views.passDraws() == 0);
        // A flat view in place of the shaded draw, then every material as it was.
        options.mesh.view = coney::debug::MeshShading::Material;
        views.draw(mesh.atomic(), coney::platform::DrawKind::Humans, false, shaded);
        CHECK(shadedDraws == 1);
        CHECK(views.passDraws() == model.clump.materials.size());
        CHECK(looksOf(mesh.atomic()) == before);
        // A second layer is left out of a flat view, and drawn under the wireframe alone.
        views.draw(mesh.bloodAtomic(), coney::platform::DrawKind::Humans, true, shaded);
        CHECK(shadedDraws == 1);
        options.mesh.view = coney::debug::MeshShading::Shaded;
        options.mesh.wireframe = coney::debug::MeshScope::Everything;
        views.draw(mesh.bloodAtomic(), coney::platform::DrawKind::Humans, true, shaded);
        CHECK(shadedDraws == 2);
    }
}

namespace {

// How many times staleDrawElements was called.
int staleDraws = 0;

// Stands in for an OpenGL entry point an earlier renderer loaded and left behind: glad keeps its pointers after that
// renderer's context is gone, so a NULL engine started after it still sees them set (issue #1268).
void APIENTRY staleDrawElements(GLenum /*mode*/, GLsizei /*count*/, GLenum /*type*/, const void* /*indices*/) {
    ++staleDraws;
}

// Puts glad's glDrawElements back however the test ends.
struct RestoreDrawElements {
    PFNGLDRAWELEMENTSPROC saved = glad_glDrawElements;
    ~RestoreDrawElements() { glad_glDrawElements = saved; }
    RestoreDrawElements() = default;
    RestoreDrawElements(const RestoreDrawElements&) = delete;
    RestoreDrawElements& operator=(const RestoreDrawElements&) = delete;
    RestoreDrawElements(RestoreDrawElements&&) = delete;
    RestoreDrawElements& operator=(RestoreDrawElements&&) = delete;
};

// Puts glad's glPolygonMode back however the test ends.
struct RestorePolygonMode {
    PFNGLPOLYGONMODEPROC saved = glad_glPolygonMode;
    ~RestorePolygonMode() { glad_glPolygonMode = saved; }
    RestorePolygonMode() = default;
    RestorePolygonMode(const RestorePolygonMode&) = delete;
    RestorePolygonMode& operator=(const RestorePolygonMode&) = delete;
    RestorePolygonMode(RestorePolygonMode&&) = delete;
    RestorePolygonMode& operator=(RestorePolygonMode&&) = delete;
};

// Stands in for a stale glPolygonMode, like staleDrawElements.
void APIENTRY stalePolygonMode(GLenum /*face*/, GLenum /*mode*/) {}

} // namespace

TEST_CASE("a flat pass on the NULL backend leaves librw alone though OpenGL was loaded before", "[mesh]") {
    // Expected: unverified, #1268 (the NULL backend draws nothing; a whole test run started OpenGL windows first)
    const RestoreDrawElements restore;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const coney::characters::CharacterModel model = fixtureModel();
    {
        coney::platform::CharacterMesh mesh(model, nullptr);
        coney::debug::DebugDrawOptions options;
        options.mesh.view = coney::debug::MeshShading::Material;
        coney::platform::MeshViews views(options);
        const std::vector<MaterialLook> before = looksOf(mesh.atomic());
        glad_glDrawElements = staleDrawElements;
        staleDraws = 0;
        views.draw(mesh.atomic(), coney::platform::DrawKind::Humans, false, [] {});
        CHECK(staleDraws == 0);
        CHECK(views.passDraws() == model.clump.materials.size());
        CHECK(looksOf(mesh.atomic()) == before);
    }
}

TEST_CASE("a wire pass on the NULL backend switches no polygon mode though OpenGL was loaded before", "[mesh]") {
    // Expected: unverified, #1268 (the wire pass decides to draw as the flat pass does: by the renderer, not glad)
    const RestorePolygonMode restore;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const coney::characters::CharacterModel model = fixtureModel();
    coney::platform::CharacterMesh mesh(model, nullptr);
    coney::debug::DebugDrawOptions options;
    options.mesh.wireframe = coney::debug::MeshScope::Everything;
    coney::platform::MeshViews views(options);
    glad_glPolygonMode = stalePolygonMode;
    coney::platform::draw::startRecording();
    views.draw(mesh.atomic(), coney::platform::DrawKind::Humans, false, [] {});
    const auto stream = coney::platform::draw::stopRecording();
    REQUIRE(stream.has_value());
    // The wireframe switches are the stream's frame operations; a wire pass would record two.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
    CHECK(stream->counts().frameOps == 0);
}

TEST_CASE("the scene counters count each draw as the kind its scope names", "[mesh][perf]") {
    // Expected: unverified, #210 (docs/guides/performance.md#counters)
    using coney::platform::DrawKind;
    const coney::platform::DrawTotals humans = coney::platform::drawTotals(DrawKind::Humans);
    const coney::platform::DrawTotals scenery = coney::platform::drawTotals(DrawKind::Scenery);
    const coney::platform::DrawTotals other = coney::platform::drawTotals(DrawKind::Other);
    {
        const coney::platform::DrawKindScope outer(DrawKind::Scenery);
        coney::platform::recordDraw(100, 300);
        {
            const coney::platform::DrawKindScope inner(DrawKind::Humans);
            coney::platform::recordDraw(10, 30);
            coney::platform::recordDraw(5, 15);
        }
        coney::platform::recordDraw(1, 3);
    }
    coney::platform::recordDraw(2, 6);
    CHECK(coney::platform::drawTotals(DrawKind::Humans).draws - humans.draws == 2);
    CHECK(coney::platform::drawTotals(DrawKind::Humans).triangles - humans.triangles == 15);
    CHECK(coney::platform::drawTotals(DrawKind::Humans).vertices - humans.vertices == 45);
    CHECK(coney::platform::drawTotals(DrawKind::Scenery).draws - scenery.draws == 2);
    CHECK(coney::platform::drawTotals(DrawKind::Scenery).triangles - scenery.triangles == 101);
    CHECK(coney::platform::drawTotals(DrawKind::Other).draws - other.draws == 1);
    // The counters' names are the Performance page's Scene line's.
    REQUIRE(coney::platform::kDrawKindNames.size() == coney::debug::kSceneCounterKinds.size());
    for (std::size_t i = 0; i < coney::platform::kDrawKindNames.size(); ++i) {
        CHECK(coney::platform::kDrawKindNames[i] == coney::debug::kSceneCounterKinds[i]);
    }
}

namespace {

// A play mode that lists its entities once it has an inspector (the front end and a loading screen have none): the
// player at the origin and a human 2 m ahead.
class ListingPlay final : public coney::debug::PlayControls {
  public:
    class Inspect final : public coney::debug::EntityInspect {
      public:
        [[nodiscard]] std::vector<coney::debug::EntityRef> entities() const override {
            return {{.id = 1,
                     .kind = coney::debug::EntityKind::Player,
                     .label = "player 1",
                     .position = coney::anim::Vec3{0.0F, 0.0F, 0.0F}},
                    {.id = 7,
                     .kind = coney::debug::EntityKind::Human,
                     .label = "AI 1",
                     .position = coney::anim::Vec3{0.0F, 2.0F, 0.0F}}};
        }
        [[nodiscard]] std::vector<coney::debug::WatchGroup> describe(coney::debug::EntityId /*id*/) const override {
            return {};
        }
    };
    [[nodiscard]] std::string sceneName() const override { return "listing"; }
    [[nodiscard]] coney::anim::Vec3 playerFeet() const override { return {}; }
    [[nodiscard]] float playerHeadingDegrees() const override { return 0.0F; }
    [[nodiscard]] float playerSpeed() const override { return 0.0F; }
    [[nodiscard]] std::string playerState() const override { return "standing"; }
    void teleport(const coney::debug::Place& /*place*/) override {}
    [[nodiscard]] std::vector<coney::debug::Place> places() const override { return {}; }
    [[nodiscard]] bool playerFrozen() const override { return false; }
    void setPlayerFrozen(bool /*frozen*/) override {}
    [[nodiscard]] coney::anim::Vec3 cameraEye() const override { return {}; }
    [[nodiscard]] coney::anim::Vec3 cameraTarget() const override { return {}; }
    void resetCamera() override {}
    [[nodiscard]] bool freeCamera() const override { return false; }
    void setFreeCamera(bool /*on*/) override {}
    [[nodiscard]] bool canSpawn() const override { return false; }
    std::expected<void, coney::Error> spawn(const coney::sandbox::Primitive& /*primitive*/) override { return {}; }
    [[nodiscard]] std::size_t spawnedCount() const override { return 0; }
    std::expected<void, coney::Error> clearSpawned() override { return {}; }
    [[nodiscard]] const coney::debug::EntityInspect* inspect() const override {
        return withInspect ? &inspector : nullptr;
    }

    Inspect inspector;
    bool withInspect = false;
};

} // namespace

TEST_CASE("a --mesh-view run waits for a play mode that lists its entities before it applies its spec", "[mesh]") {
    // Expected: unverified, #210 (docs/guides/running-options.md: --mesh-view applies from the first frame of play)
    ListingPlay play; // the front end, a loading screen: no inspector yet
    coney::debug::DebugServices services;
    services.play = [&play]() -> coney::debug::PlayControls* { return &play; };
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession session(tunables, services, nullptr);
    auto spec = coney::debug::parseMeshViewSpec("wire=all,view=material,normals,select=nearest,frames=2");
    REQUIRE(spec.has_value());
    coney::platform::MeshViewRun run(*spec);
    for (int i = 0; i < 5; ++i) {
        run.frame(session);
    }
    CHECK_FALSE(coney::debug::meshViewsOn(session.debugDraw().mesh));
    CHECK(session.debugDraw().inspected == coney::debug::kNoEntity);

    // The first frame of a play mode that lists its entities applies the spec, with the nearest human selected.
    play.withInspect = true;
    run.frame(session);
    CHECK(coney::debug::meshViewsOn(session.debugDraw().mesh));
    CHECK(session.debugDraw().inspected == 7);
    run.frame(session);
    CHECK(coney::debug::meshViewsOn(session.debugDraw().mesh));
    // After its two frames the views go off and the selection with them.
    run.frame(session);
    CHECK_FALSE(coney::debug::meshViewsOn(session.debugDraw().mesh));
    CHECK(session.debugDraw().inspected == coney::debug::kNoEntity);
}

namespace {

// A hook that notes each model it is handed and draws nothing (librw's NULL device cannot render an atomic, so the
// game's own draw, `shaded`, is left uncalled).
class NotingHook final : public coney::platform::MeshViewHook {
  public:
    struct Call {
        const rw::Atomic* atomic = nullptr;
        coney::platform::DrawKind kind = coney::platform::DrawKind::Other;
        bool layer = false;
    };
    bool beginFrame(const coney::platform::PlayLevelMode&) override { return true; }
    void draw(rw::Atomic* atomic, coney::platform::DrawKind kind, bool layer, const std::function<void()>&) override {
        calls.push_back(Call{.atomic = atomic, .kind = kind, .layer = layer});
    }
    std::vector<Call> calls;
};

} // namespace

TEST_CASE("the lighting rig hands each atomic to the mesh views' hook as the kind in scope", "[mesh]") {
    // Expected: unverified, #210 (the hook sits in the rig's last step; the kind is the caller's DrawKindScope)
    using coney::platform::DrawKind;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const coney::characters::CharacterModel model = fixtureModel();
    {
        coney::platform::CharacterMesh mesh(model, nullptr);
        coney::platform::CharacterMesh second(model, nullptr); // stands in for a second layer: the rig marks any atomic
        coney::graphics::LevelLighting level;
        coney::platform::SceneLighting scene(level, std::nullopt, std::nullopt);
        NotingHook hook;
        scene.beginFrame(coney::world::CameraPose{}, 0.1F, 100.0F, 0);
        CHECK(scene.atomicsDrawn() == 0);

        // It is handed each atomic with the kind the caller's scope names and a marked layer as a layer; every atomic
        // is counted.
        scene.setMeshViews(&hook);
        {
            const coney::platform::DrawKindScope kind(DrawKind::Humans);
            scene.drawHumanAtomic(mesh.atomic(), false);
            scene.setMeshLayer(second.atomic());
            scene.drawHumanAtomic(second.atomic(), false);
            scene.setMeshLayer(nullptr);
        }
        {
            const coney::platform::DrawKindScope kind(DrawKind::Objects);
            scene.drawObjectAtomic(mesh.atomic());
        }
        REQUIRE(hook.calls.size() == 3);
        CHECK(hook.calls[0].atomic == mesh.atomic());
        CHECK(hook.calls[0].kind == DrawKind::Humans);
        CHECK_FALSE(hook.calls[0].layer);
        CHECK(hook.calls[1].atomic == second.atomic());
        CHECK(hook.calls[1].kind == DrawKind::Humans);
        CHECK(hook.calls[1].layer);
        CHECK(hook.calls[2].atomic == mesh.atomic());
        CHECK(hook.calls[2].kind == DrawKind::Objects);
        CHECK_FALSE(hook.calls[2].layer);
        CHECK(scene.atomicsDrawn() == 3);
        scene.setMeshViews(nullptr);
    }
}
