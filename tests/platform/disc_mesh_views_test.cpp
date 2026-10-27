// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc of the mesh views' play-mode half (docs/guides/debug-menu.md#mesh-views), what
// the debug side reads: what the play mode hands the views' hook as it draws, the Entity inspector's Mesh group of a
// drawn human and the passes it counts, the selection's normals and bone axes, and `--mesh-view`'s run over a real
// inspector. What the views draw, and that with every view off the game draws as it does without them, is in
// disc_mesh_views_draw_test.cpp. They run only when the environment variable CONEY_DISC names the disc and skip
// otherwise; they print counts only (LEGAL.md).

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "debug/debug_session.h"
#include "debug/entity_inspect.h"
#include "debug/mesh_views.h"
#include "debug/play_controls.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "human/player.h"
#include "platform/character_skin.h"
#include "platform/game_session.h"
#include "platform/mesh_views.h"
#include "platform/perf_probes.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"
#include "platform/window.h"
#include "support/disc_mesh_views_fixtures.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

using coney::debug::EntityId;
using coney::platform::DrawKind;
using coney::platform::PlayLevelControls;
using coney::test::mesh_views::humanIds;
using coney::test::mesh_views::kSkipScene;
using coney::test::mesh_views::MeshFacts;
using coney::test::mesh_views::meshFactsOf;
using coney::test::mesh_views::Opened;
using coney::test::mesh_views::openYard;
using coney::test::mesh_views::playLevel;
using coney::test::mesh_views::RecordingHook;
using coney::test::mesh_views::testWindow;
using coney::test::mesh_views::windowAvailable;
using coney::test::mesh_views::Yard;

} // namespace

TEST_CASE("a level played through the session asks the mesh views' hook at every frame it renders",
          "[disc][story][mesh]") {
    // Expected: unverified, #210 (the hook is Coney's own debug tooling: the session passes it to the play mode)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kSkipScene);
    const auto hook = std::make_shared<RecordingHook>();
    run.session().setMeshViews(hook);
    REQUIRE(playLevel(run, "level99"));
    run.run(10);
    const coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);

    // Asked once at each frame of play, about the play mode the session made.
    const int begunBefore = hook->begun;
    run.run(3);
    CHECK(hook->begun == begunBefore + 3);
    CHECK(hook->lastMode == play);

    // A hook that says it is off is still asked every frame and is handed nothing.
    hook->on = false;
    run.run(2);
    CHECK(hook->begun == begunBefore + 5);
    CHECK(hook->calls.empty());
    std::printf("mesh views: level99, the hook asked at each of %d frames\n", hook->begun - begunBefore);
}

TEST_CASE(
    "a level drawn in a window hands the mesh views' hook its scenery, humans and objects, and each human's passes",
    "[disc][story][mesh][gl]") {
    // Expected: unverified, #210 (the models are the play mode's own draws; the passes are the rig's atomics)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    if (!windowAvailable()) {
        SKIP("no OpenGL window: the Null renderer draws no model");
    }
    const coney::platform::WindowDesc window = testWindow();
    coney::test::DiscSession run(*wad, kSkipScene,
                                 {.skipMovies = true, .rumble = std::nullopt, .profiles = std::nullopt}, &window);
    const auto hook = std::make_shared<RecordingHook>();
    run.session().setMeshViews(hook);
    REQUIRE(playLevel(run, "level99"));
    run.run(10);
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const coney::platform::PlayLevelControls controls(play);
    REQUIRE(controls.inspect() != nullptr);

    // The frame's draws, by kind.
    const auto count = [&hook](DrawKind kind, bool layer) {
        return std::ranges::count_if(hook->calls, [kind, layer](const RecordingHook::Call& call) {
            return call.kind == kind && call.layer == layer;
        });
    };
    const auto drawnAtomic = [&hook](const rw::Atomic* atomic) {
        return std::ranges::count_if(hook->calls,
                                     [atomic](const RecordingHook::Call& call) { return call.atomic == atomic; });
    };
    CHECK(count(DrawKind::Scenery, false) > 0);
    CHECK(count(DrawKind::Humans, false) > 0);
    CHECK(count(DrawKind::Scenery, true) == 0);

    // The player's body is drawn once; the passes the Mesh group counts are that and, with blood on him, the blood
    // layer, which the hook is handed marked as a layer.
    const std::vector<const rw::Atomic*> body =
        PlayLevelControls::atomicsOf(*play, PlayLevelControls::kInspectPlayerId);
    REQUIRE(body.size() == 2);
    CHECK(drawnAtomic(body[0]) == 1);
    const std::optional<MeshFacts> found = meshFactsOf(*controls.inspect(), PlayLevelControls::kInspectPlayerId);
    REQUIRE(found.has_value());
    const MeshFacts facts = found.value_or(MeshFacts{});
    CHECK(facts.passes == 1 + static_cast<std::size_t>(drawnAtomic(body[1])));
    CHECK(count(DrawKind::Humans, true) == drawnAtomic(body[1]));
    CHECK(facts.drawCalls == facts.materials * facts.passes);
    // No other layer is marked: only a human's blood.
    CHECK(count(DrawKind::Humans, true) + count(DrawKind::Objects, true) + count(DrawKind::Other, true) ==
          drawnAtomic(body[1]));
    std::printf("mesh views: level99 in a window, %td scenery, %td human and %td object draws a frame; the player's "
                "%zu pass(es)\n",
                count(DrawKind::Scenery, false), count(DrawKind::Humans, false), count(DrawKind::Objects, false),
                facts.passes);

    // Off for a frame, the hook is asked and not called, the game draws the same passes, and the next frame it is
    // handed the models again.
    hook->on = false;
    run.run(1);
    CHECK(hook->calls.empty());
    const std::optional<MeshFacts> foundOff = meshFactsOf(*controls.inspect(), PlayLevelControls::kInspectPlayerId);
    REQUIRE(foundOff.has_value());
    const MeshFacts off = foundOff.value_or(MeshFacts{});
    CHECK(off.passes == facts.passes);
    hook->on = true;
    run.run(1);
    CHECK_FALSE(hook->calls.empty());
}

TEST_CASE("the Mesh group counts a drawn human's model, and what is not a drawn human has none", "[disc][mesh]") {
    // Expected: unverified, #210 (the counts are librw's geometry of the model the play mode draws the human with)
    Yard yard;
    // Drawn in a window where there is one, so that the passes are counted; else the structure alone.
    Opened opened = windowAvailable() ? openYard(yard, true) : Opened::NoWindow;
    if (opened == Opened::NoWindow) {
        opened = openYard(yard, false);
    }
    if (opened == Opened::NoDisc) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const bool drawn = opened == Opened::Window;
    coney::platform::PlayLevelMode& mode = *yard.mode;
    const coney::platform::PlayLevelControls controls(&mode);
    REQUIRE(controls.inspect() != nullptr);
    const std::vector<EntityId> humans = humanIds(*controls.inspect());
    REQUIRE(humans.size() >= 3);

    // Every human the mode draws has a Mesh group, two atomics (the body and the blood layer), and the triangles and
    // materials of its geometry: the player's, a fighter's.
    for (const EntityId id : humans) {
        const std::vector<const rw::Atomic*> atomics = PlayLevelControls::atomicsOf(mode, id);
        REQUIRE(atomics.size() == 2);
        REQUIRE(atomics[0] != nullptr);
        REQUIRE(atomics[1] != nullptr);
        CHECK(atomics[0] != atomics[1]);
        const std::optional<MeshFacts> found = meshFactsOf(*controls.inspect(), id);
        REQUIRE(found.has_value());
        const MeshFacts facts = found.value_or(MeshFacts{});
        CHECK(facts.triangles == static_cast<std::size_t>(atomics[0]->geometry->numTriangles));
        CHECK(facts.materials == static_cast<std::size_t>(atomics[0]->geometry->matList.numMaterials));
        CHECK(facts.drawCalls == facts.materials * facts.passes);
        // Where it is skinned: the vertex shader once the engine runs the skin pipeline (a window), else the CPU.
        CHECK(facts.skinnedOn == (drawn && coney::platform::characterSkinOn() ? "GPU" : "CPU"));
        // A human near the player is drawn in one pass, or two with blood on him; the Null renderer draws none.
        if (drawn) {
            CHECK(facts.passes >= 1);
            CHECK(facts.passes <= 2);
        } else {
            CHECK(facts.passes == 0);
        }
    }
    // Different humans have different atomics.
    CHECK(PlayLevelControls::atomicsOf(mode, humans[0])[0] != PlayLevelControls::atomicsOf(mode, humans[1])[0]);

    // What is not a drawn human has no atomics and no Mesh group: an object, an id nothing has, no selection.
    for (const coney::debug::EntityRef& entity : controls.inspect()->entities()) {
        if (entity.kind == coney::debug::EntityKind::Object) {
            CHECK(PlayLevelControls::atomicsOf(mode, entity.id).empty());
            CHECK_FALSE(meshFactsOf(*controls.inspect(), entity.id).has_value());
        }
    }
    CHECK(PlayLevelControls::atomicsOf(mode, 0x7777777ULL).empty());
    CHECK(PlayLevelControls::atomicsOf(mode, coney::debug::kNoEntity).empty());
    CHECK_FALSE(meshFactsOf(*controls.inspect(), coney::debug::kNoEntity).has_value());

    // A fighter far past the draw distance is not drawn: no passes, no draw calls.
    const coney::anim::Vec3 far = coney::debug::spotAhead(mode.playerFeet(), mode.playerHeadingDegrees(), 4000.0F);
    REQUIRE(mode.spawnFighter(far, 0.0F));
    yard.run(2);
    const std::vector<EntityId> after = humanIds(*controls.inspect());
    REQUIRE(after.size() == humans.size() + 1);
    const std::optional<MeshFacts> foundDistant = meshFactsOf(*controls.inspect(), after.back());
    REQUIRE(foundDistant.has_value());
    const MeshFacts distant = foundDistant.value_or(MeshFacts{});
    CHECK(distant.passes == 0);
    CHECK(distant.drawCalls == 0);

    // The fighters the debug menus spawned (the two ahead and the far one, the last AI humans listed) are gone once
    // they are cleared: no Mesh group, no atomics. The layout's own humans stay.
    const std::vector<EntityId> spawned{humans[humans.size() - 2], humans.back(), after.back()};
    mode.clearFighters();
    yard.run(5);
    for (const EntityId id : spawned) {
        CHECK(PlayLevelControls::atomicsOf(mode, id).empty());
        CHECK_FALSE(meshFactsOf(*controls.inspect(), id).has_value());
    }
    CHECK(meshFactsOf(*controls.inspect(), humans.front()).has_value());
    std::printf("mesh group: %zu humans listed, drawn in a window: %s\n", humans.size(), drawn ? "yes" : "no");
}

TEST_CASE("the selection's normals and bone axes are lines on a human and on nothing else", "[disc][mesh]") {
    // Expected: unverified, #210 (the lines are the posed model's, placed as the play mode draws it)
    Yard yard;
    if (openYard(yard, false) == Opened::NoDisc) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::platform::PlayLevelMode& mode = *yard.mode;
    const coney::platform::PlayLevelControls controls(&mode);
    const std::vector<EntityId> humans = humanIds(*controls.inspect());
    REQUIRE(humans.size() >= 2);
    const coney::human::PlayerSnapshot snapshot =
        coney::human::interpolate(mode.player().previous(), mode.player().current(), 1.0F);
    coney::debug::DebugDrawOptions options;
    const coney::platform::SceneLines linesFor = PlayLevelControls::sceneLinesFor(options);
    const auto linesNow = [&] { return linesFor(mode, snapshot, 1.0F).size(); };

    // Nothing on, nothing selected: the Debug draw page's own lines only (none with every one of them off).
    const std::size_t base = linesNow();
    options.inspected = humans[1];
    options.highlight = false;
    const std::size_t selectedOnly = linesNow();
    options.mesh.boneAxes = true;
    const std::size_t withBones = linesNow();
    CHECK(withBones > selectedOnly);
    options.mesh.boneAxes = false;
    options.mesh.normals = true;
    const std::size_t withNormals = linesNow();
    CHECK(withNormals > selectedOnly);
    options.mesh.boneAxes = true;
    CHECK(linesNow() == (withBones - selectedOnly) + (withNormals - selectedOnly) + selectedOnly);

    // No selection or an object: no lines of the model.
    options.inspected = coney::debug::kNoEntity;
    CHECK(linesNow() == base);
    for (const coney::debug::EntityRef& entity : controls.inspect()->entities()) {
        if (entity.kind == coney::debug::EntityKind::Object) {
            options.inspected = entity.id;
            CHECK(linesNow() == base);
            break;
        }
    }
    // The player's own lines follow his pose: they are there for him too.
    options.inspected = humans[0];
    CHECK(linesNow() > selectedOnly);
    std::printf("mesh lines: %zu bone-axis lines, %zu normal lines on one human\n", withBones - selectedOnly,
                withNormals - selectedOnly);
}

TEST_CASE("a --mesh-view run applies its spec to a real inspector and switches the views off after its frames",
          "[disc][mesh]") {
    // Expected: unverified, #210 (docs/guides/running-options.md: --mesh-view, frames=N)
    Yard yard;
    if (openYard(yard, false) == Opened::NoDisc) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::platform::PlayLevelControls controls;
    coney::debug::DebugServices services;
    services.play = [&yard, &controls] { return controls.controlsOf(yard.mode.get()); };
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession session(tunables, services, nullptr);

    auto spec = coney::debug::parseMeshViewSpec("wire=all,view=material,normals,bones,select=nearest,frames=3");
    REQUIRE(spec.has_value());
    coney::platform::MeshViewRun run(*spec);
    REQUIRE_FALSE(coney::debug::meshViewsOn(session.debugDraw().mesh));

    // The first frame: the spec's views are on and the nearest other human is selected, not the player.
    run.frame(session);
    CHECK(session.debugDraw().mesh.wireframe == coney::debug::MeshScope::Everything);
    CHECK(session.debugDraw().mesh.view == coney::debug::MeshShading::Material);
    CHECK(session.debugDraw().mesh.normals);
    CHECK(session.debugDraw().mesh.boneAxes);
    const std::vector<EntityId> humans = humanIds(*session.play()->inspect());
    REQUIRE(humans.size() >= 3);
    CHECK(session.debugDraw().inspected != coney::debug::kNoEntity);
    CHECK(session.debugDraw().inspected != humans.front());
    CHECK(std::ranges::count(humans, session.debugDraw().inspected) == 1);

    // The spec's frames: on while they last, then every view off and the selection dropped.
    run.frame(session);
    run.frame(session);
    CHECK(coney::debug::meshViewsOn(session.debugDraw().mesh));
    run.frame(session);
    CHECK_FALSE(coney::debug::meshViewsOn(session.debugDraw().mesh));
    CHECK(session.debugDraw().inspected == coney::debug::kNoEntity);
    // Done: the page's own switches afterwards are left alone.
    session.debugDraw().mesh.normals = true;
    run.frame(session);
    CHECK(session.debugDraw().mesh.normals);

    // `select=player` and no frames: the player is selected and the views stay on.
    auto keep = coney::debug::parseMeshViewSpec("view=draw,select=player");
    REQUIRE(keep.has_value());
    coney::platform::MeshViewRun forever(*keep);
    for (int i = 0; i < 10; ++i) {
        forever.frame(session);
    }
    CHECK(session.debugDraw().mesh.view == coney::debug::MeshShading::DrawCall);
    CHECK(session.debugDraw().inspected == humans.front());
}
