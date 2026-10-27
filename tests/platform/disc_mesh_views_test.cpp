// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc of the mesh views' play-mode half (docs/guides/debug-menu.md#mesh-views): what
// the play mode hands the views' hook as it draws, the Entity inspector's Mesh group of a drawn human and the passes it
// counts, the selection's normals and bone axes, `--mesh-view`'s run over a real inspector, and that with every view
// off the game draws as it does without them: the same passes, no call to the hook, and, on a real window where one can
// be had, the same frame pixel for pixel. The Null renderer the tests run on draws no pixels, so what CI sees of "no
// pixel changes" is the draws' bookkeeping; the pixel check skips without an OpenGL window. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "ai/ai_config.h"
#include "characters/character_types.h"
#include "core/game_timer.h"
#include "debug/debug_session.h"
#include "debug/entity_inspect.h"
#include "debug/menu_model.h"
#include "debug/mesh_views.h"
#include "debug/play_controls.h"
#include "debug/tunables.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_start.h"
#include "graphics/render_device.h"
#include "human/player.h"
#include "platform/character_skin.h"
#include "platform/game_session.h"
#include "platform/game_session_debug.h"
#include "platform/mesh_view_hook.h"
#include "platform/mesh_views.h"
#include "platform/perf_probes.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/sandbox_play_scenery.h"
#include "sandbox/sandbox_world.h"
#include "scripting/config_strings.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/test_paths.h"

namespace {

// How long the level may take to give the player his pad: 60 s of frames.
constexpr std::uint64_t kLimit = 1800;

// A pad script that taps cross every second for the first 20 s of play: it skips the level's opening scene.
constexpr const char* kSkipScene = "30 tap cross\n60 tap cross\n90 tap cross\n120 tap cross\n150 tap cross\n"
                                   "180 tap cross\n210 tap cross\n240 tap cross\n270 tap cross\n300 tap cross\n"
                                   "330 tap cross\n360 tap cross\n390 tap cross\n420 tap cross\n450 tap cross\n"
                                   "480 tap cross\n510 tap cross\n540 tap cross\n570 tap cross\n600 tap cross\n";

using coney::debug::EntityId;
using coney::platform::DrawKind;
using coney::platform::PlayLevelControls;

// A hook that records what the play mode hands it and draws every model as the game does. `on` is what beginFrame()
// answers.
class RecordingHook final : public coney::platform::MeshViewHook {
  public:
    // One model handed to draw().
    struct Call {
        const rw::Atomic* atomic = nullptr;
        DrawKind kind = DrawKind::Other;
        bool layer = false;
    };

    bool beginFrame(const coney::platform::PlayLevelMode& mode) override {
        ++begun;
        calls.clear();
        lastMode = &mode;
        return on;
    }
    void draw(rw::Atomic* atomic, DrawKind kind, bool layer, const std::function<void()>& shaded) override {
        calls.push_back(Call{.atomic = atomic, .kind = kind, .layer = layer});
        shaded();
    }

    bool on = true;
    int begun = 0;           // the frames it was asked about
    std::vector<Call> calls; // this frame's draws
    const coney::platform::PlayLevelMode* lastMode = nullptr;
};

// The whole numbers in `text`, in order: "6 (3 materials x 2 passes)" holds 6, 3 and 2.
std::vector<std::size_t> numbersIn(std::string_view text) {
    std::vector<std::size_t> numbers;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') {
            continue;
        }
        std::size_t value = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            value = value * 10 + static_cast<std::size_t>(text[i] - '0');
            ++i;
        }
        numbers.push_back(value);
    }
    return numbers;
}

// What the Entity inspector says of `id`'s mesh: the passes the last frame drew and the materials of the model;
// nothing when it has no Mesh group.
struct MeshFacts {
    std::size_t drawCalls = 0;
    std::size_t materials = 0;
    std::size_t passes = 0;
    std::size_t triangles = 0;
    std::string skinnedOn;
};
std::optional<MeshFacts> meshFactsOf(const coney::debug::EntityInspect& inspect, EntityId id) {
    const std::vector<coney::debug::WatchGroup> groups = inspect.describe(id);
    const std::optional<std::string> calls = coney::debug::watchValue(groups, "Mesh", "Draw calls last frame");
    if (!calls) {
        return std::nullopt;
    }
    const std::vector<std::size_t> numbers = numbersIn(*calls);
    REQUIRE(numbers.size() == 3);
    MeshFacts facts{.drawCalls = numbers[0], .materials = numbers[1], .passes = numbers[2]};
    const std::optional<std::string> triangles = coney::debug::watchValue(groups, "Mesh", "Triangles");
    REQUIRE(triangles.has_value());
    facts.triangles = numbersIn(*triangles).front();
    facts.skinnedOn = coney::debug::watchValue(groups, "Mesh", "Skinned on").value_or("");
    return facts;
}

// The ids of the humans the inspector lists, player 1 first.
std::vector<EntityId> humanIds(const coney::debug::EntityInspect& inspect) {
    std::vector<EntityId> ids;
    for (const coney::debug::EntityRef& entity : inspect.entities()) {
        if (entity.kind == coney::debug::EntityKind::Player || entity.kind == coney::debug::EntityKind::Human) {
            ids.push_back(entity.id);
        }
    }
    return ids;
}

// The sandbox's fight yard on level99's configuration, started and stepped on the fixed timestep, on the Null renderer
// or a hidden OpenGL window.
struct Yard {
    std::optional<coney::io::Wad> wad;
    std::unique_ptr<coney::platform::RenderEngine> engine;
    std::unique_ptr<coney::platform::PlayLevelMode> mode;
    coney::GameModeStack modes;
    coney::GameTimer timer;

    // Steps and draws the world `count` frames.
    void run(int count) { (void)modes.runUntilEmpty(timer, {}, count); }
};

// What opening a yard came to.
enum class Opened : std::uint8_t { NoDisc, NoWindow, Null, Window };

// The window the tests draw in: hidden, the logical screen's shape.
coney::platform::WindowDesc testWindow() {
    coney::platform::WindowDesc desc;
    desc.title = "Coney mesh views check";
    desc.width = 320;
    desc.height = 240;
    desc.hidden = true;
    desc.logicalFrame = true;
    return desc;
}

// Whether the machine can open an OpenGL window to draw in (a display and a context; CI has none).
bool windowAvailable() {
    const coney::platform::WindowDesc desc = testWindow();
    return coney::platform::RenderEngine::start(coney::platform::RenderBackend::OpenGl, desc).has_value();
}

// Opens the yard from CONEY_DISC, on the Null renderer or (`window`) a hidden OpenGL window, with two fighters ahead of
// the player so there are humans to look at. The Null renderer draws nothing: the play mode's frames then step the
// world and ask the views' hook, and no model is drawn.
Opened openYard(Yard& yard, bool window) {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return Opened::NoDisc;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    yard.wad = std::move(*wad);
    const coney::platform::WindowDesc desc = testWindow();
    auto engine = window ? coney::platform::RenderEngine::start(coney::platform::RenderBackend::OpenGl, desc)
                         : coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    if (!engine) {
        return Opened::NoWindow;
    }
    yard.engine = std::move(*engine);

    // The sandbox's setup as main makes it: level99's configuration.
    const coney::LevelScriptRun run =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(*yard.wad), "level99", 1, {});
    coney::platform::PlayerSetup setup;
    setup.ai = coney::ai::aiConfigFrom(run.recorded);
    setup.types = coney::characters::CharacterTypes::fromRecorded(run.recorded);
    auto world = coney::sandbox::SandboxWorld::load(coney::test::assetsDir() / "sandbox", "combat");
    REQUIRE(world.has_value());
    auto made = coney::platform::createPlayInSandbox(
        *yard.engine, *yard.wad, std::move(*world), std::nullopt, [](std::string_view) {}, setup);
    REQUIRE(made.has_value());
    yard.mode = std::move(*made);
    yard.timer.setFixedStep(true);
    yard.modes.push(*yard.mode);
    yard.run(5);
    const coney::anim::Vec3 ahead =
        coney::debug::spotAhead(yard.mode->playerFeet(), yard.mode->playerHeadingDegrees(), 4.0F);
    REQUIRE(yard.mode->spawnFighter(coney::anim::Vec3{ahead.x - 1.5F, ahead.y, ahead.z},
                                    yard.mode->playerHeadingDegrees() + 180.0F));
    REQUIRE(yard.mode->spawnFighter(coney::anim::Vec3{ahead.x + 1.5F, ahead.y, ahead.z},
                                    yard.mode->playerHeadingDegrees() + 180.0F));
    yard.run(2);
    return window ? Opened::Window : Opened::Null;
}

// The passes of every human the inspector lists, in its order: what a frame of the world drew of them.
std::vector<std::size_t> passesOfHumans(const coney::platform::PlayLevelControls& controls) {
    std::vector<std::size_t> passes;
    for (const EntityId id : humanIds(*controls.inspect())) {
        const std::optional<MeshFacts> facts = meshFactsOf(*controls.inspect(), id);
        passes.push_back(facts ? facts->passes : 0);
    }
    return passes;
}

// A level of the game session, started and played until the opening scene is skipped and the player has his pad, on the
// Null renderer or (`window`) a hidden OpenGL window; false (and the run unfinished) when it does not get there.
bool playLevel(coney::test::DiscSession& run, std::string_view level) {
    run.session().startAtLevel(level, 1);
    if (!run.runUntilPlay()) {
        return false;
    }
    return run.runUntil(
        [&run] { return run.session().play() != nullptr && run.session().play()->player().padControlled(); }, kLimit);
}

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
    const std::optional<MeshFacts> facts = meshFactsOf(*controls.inspect(), PlayLevelControls::kInspectPlayerId);
    REQUIRE(facts.has_value());
    CHECK(facts->passes == 1 + static_cast<std::size_t>(drawnAtomic(body[1])));
    CHECK(count(DrawKind::Humans, true) == drawnAtomic(body[1]));
    CHECK(facts->drawCalls == facts->materials * facts->passes);
    // No other layer is marked: only a human's blood.
    CHECK(count(DrawKind::Humans, true) + count(DrawKind::Objects, true) + count(DrawKind::Other, true) ==
          drawnAtomic(body[1]));
    std::printf("mesh views: level99 in a window, %td scenery, %td human and %td object draws a frame; the player's "
                "%zu pass(es)\n",
                count(DrawKind::Scenery, false), count(DrawKind::Humans, false), count(DrawKind::Objects, false),
                facts->passes);

    // Off for a frame, the hook is asked and not called, the game draws the same passes, and the next frame it is
    // handed the models again.
    hook->on = false;
    run.run(1);
    CHECK(hook->calls.empty());
    const std::optional<MeshFacts> off = meshFactsOf(*controls.inspect(), PlayLevelControls::kInspectPlayerId);
    REQUIRE(off.has_value());
    CHECK(off->passes == facts->passes);
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
        const std::optional<MeshFacts> facts = meshFactsOf(*controls.inspect(), id);
        REQUIRE(facts.has_value());
        CHECK(facts->triangles == static_cast<std::size_t>(atomics[0]->geometry->numTriangles));
        CHECK(facts->materials == static_cast<std::size_t>(atomics[0]->geometry->matList.numMaterials));
        CHECK(facts->drawCalls == facts->materials * facts->passes);
        // Where it is skinned: the vertex shader once the engine runs the skin pipeline (a window), else the CPU.
        CHECK(facts->skinnedOn == (drawn && coney::platform::characterSkinOn() ? "GPU" : "CPU"));
        // A human near the player is drawn in one pass, or two with blood on him; the Null renderer draws none.
        if (drawn) {
            CHECK(facts->passes >= 1);
            CHECK(facts->passes <= 2);
        } else {
            CHECK(facts->passes == 0);
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
    const std::optional<MeshFacts> distant = meshFactsOf(*controls.inspect(), after.back());
    REQUIRE(distant.has_value());
    CHECK(distant->passes == 0);
    CHECK(distant->drawCalls == 0);

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

TEST_CASE("with every mesh view off the game draws as without the hook: the same passes and no call to it",
          "[disc][mesh][gl]") {
    // Expected: unverified, #210 (faithful first: the views change nothing while off)
    constexpr int kFrames = 20;
    enum class Hook : std::uint8_t { None, ViewsOff, RecordingOff, ViewsOn };
    // What one run of the yard came to: the passes every human was drawn in each frame, how often the hook was asked
    // and handed a model, and the flat passes the views drew.
    struct Run {
        Opened opened = Opened::NoDisc;
        std::vector<std::vector<std::size_t>> passes;
        int asked = 0;
        std::size_t handed = 0;
        std::size_t flatDraws = 0;
    };
    // The world is the same each run, so the passes the game draws are too, whatever the hook does.
    const auto play = [](Hook which) {
        Run result;
        Yard yard;
        result.opened = windowAvailable() ? openYard(yard, true) : Opened::NoWindow;
        if (result.opened != Opened::Window) {
            return result;
        }
        coney::debug::DebugDrawOptions options;
        if (which == Hook::ViewsOn) {
            options.mesh.view = coney::debug::MeshShading::Material;
            options.mesh.viewScope = coney::debug::MeshScope::Humans;
        }
        std::shared_ptr<coney::platform::MeshViews> views;
        std::shared_ptr<RecordingHook> recording;
        if (which == Hook::ViewsOff || which == Hook::ViewsOn) {
            views = std::make_shared<coney::platform::MeshViews>(options);
            yard.mode->setMeshViews(views);
        } else if (which == Hook::RecordingOff) {
            recording = std::make_shared<RecordingHook>();
            recording->on = false;
            yard.mode->setMeshViews(recording);
        }
        const coney::platform::PlayLevelControls controls(yard.mode.get());
        for (int i = 0; i < kFrames; ++i) {
            yard.run(1);
            result.passes.push_back(passesOfHumans(controls));
            if (views) {
                result.flatDraws += views->passDraws();
            }
            if (recording) {
                result.asked = recording->begun;
                result.handed += recording->calls.size();
            }
        }
        return result;
    };
    const Run plain = play(Hook::None);
    if (plain.opened == Opened::NoWindow) {
        SKIP("no OpenGL window: the Null renderer draws no model");
    }
    if (plain.opened == Opened::NoDisc) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    REQUIRE(plain.passes.size() == static_cast<std::size_t>(kFrames));
    // The humans are drawn at all: someone is in one pass or two.
    std::size_t drawnPasses = 0;
    for (const std::size_t passes : plain.passes.back()) {
        drawnPasses += passes;
    }
    CHECK(drawnPasses >= 3);

    // Off, through the real views or a hook that says no: the same passes, nothing drawn by the hook.
    const Run off = play(Hook::ViewsOff);
    CHECK(off.passes == plain.passes);
    CHECK(off.flatDraws == 0);
    const Run recordingOff = play(Hook::RecordingOff);
    CHECK(recordingOff.passes == plain.passes);
    CHECK(recordingOff.asked >= kFrames);
    CHECK(recordingOff.handed == 0);

    // On, the passes the game draws are still the same (the hook only adds its own), and the views draw theirs.
    const Run on = play(Hook::ViewsOn);
    CHECK(on.passes == plain.passes);
    CHECK(on.flatDraws > 0);
    std::printf("views off: %d frames, the humans' passes equal with no hook, a hook that is off and a flat view on "
                "(%zu flat draws)\n",
                kFrames, on.flatDraws);
}

TEST_CASE("the flat view draws the selection alone, or every human, and the models in scope", "[disc][mesh][gl]") {
    // Expected: unverified, #210 (docs/guides/debug-menu.md#mesh-views: the scope of a view)
    if (!windowAvailable()) {
        SKIP("no OpenGL window: the Null renderer draws no model");
    }
    Yard yard;
    if (openYard(yard, true) == Opened::NoDisc) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::platform::PlayLevelMode& mode = *yard.mode;
    const coney::platform::PlayLevelControls controls(&mode);
    const std::vector<EntityId> humans = humanIds(*controls.inspect());
    REQUIRE(humans.size() >= 3);
    coney::debug::DebugDrawOptions options;
    options.mesh.view = coney::debug::MeshShading::Material;
    options.mesh.viewScope = coney::debug::MeshScope::Selection;
    options.inspected = humans[1];
    const auto views = std::make_shared<coney::platform::MeshViews>(options);
    mode.setMeshViews(views);

    // The selection alone: a flat pass over each of its materials, none over anyone else.
    const std::optional<MeshFacts> selected = meshFactsOf(*controls.inspect(), humans[1]);
    REQUIRE(selected.has_value());
    yard.run(1);
    CHECK(views->passDraws() == selected->materials);

    // Every human: each one drawn as a body gets its materials' passes, the blood layer none.
    options.mesh.viewScope = coney::debug::MeshScope::Humans;
    yard.run(1);
    std::size_t expected = 0;
    for (const EntityId id : humans) {
        const std::optional<MeshFacts> facts = meshFactsOf(*controls.inspect(), id);
        REQUIRE(facts.has_value());
        expected += facts->passes > 0 ? facts->materials : 0;
    }
    CHECK(views->passDraws() == expected);
    CHECK(expected > selected->materials);

    // Everything: the sandbox's own blocks are drawn by its renderer, so the humans are all there is to draw here.
    options.mesh.viewScope = coney::debug::MeshScope::Everything;
    yard.run(1);
    CHECK(views->passDraws() >= expected);

    // No selection and the selection's scope: nothing is drawn flat, and the models are drawn as the game does.
    options.mesh.viewScope = coney::debug::MeshScope::Selection;
    options.inspected = coney::debug::kNoEntity;
    yard.run(1);
    CHECK(views->passDraws() == 0);
    std::printf("flat view: %zu passes on one human, %zu on every human\n", selected->materials, expected);
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

TEST_CASE("with every mesh view off a real frame is the same pixel for pixel as without the hook, and after "
          "--mesh-view's frames too",
          "[disc][mesh][gl]") {
    // Expected: unverified, #210 (faithful first: the views change no pixel while off)
    constexpr int kFrames = 24;
    constexpr int kOnFrames = 6;
    // The hash of every frame of a run of the yard on a hidden OpenGL window: with `views` as the hook (none: no hook);
    // the flat view over everything for the first `onFor` frames when positive. Empty when there is no disc or window.
    enum class Skip : std::uint8_t { None, NoDisc, NoWindow };
    Skip skipped = Skip::None;
    const auto frames = [&skipped](std::shared_ptr<coney::platform::MeshViews> views,
                                   coney::debug::DebugDrawOptions& options, int onFor) {
        std::vector<std::uint64_t> hashes;
        Yard yard;
        const Opened opened = windowAvailable() ? openYard(yard, true) : Opened::NoWindow;
        if (opened != Opened::Window) {
            skipped = opened == Opened::NoDisc ? Skip::NoDisc : Skip::NoWindow;
            return hashes;
        }
        yard.mode->setMeshViews(std::move(views));
        yard.engine->setFrameTap([&hashes](std::span<const std::uint8_t> pixels, coney::graphics::Extent) {
            // FNV-1a over the frame's bytes.
            std::uint64_t hash = 0xcbf29ce484222325ULL;
            for (const std::uint8_t byte : pixels) {
                hash = (hash ^ byte) * 0x100000001b3ULL;
            }
            hashes.push_back(hash);
        });
        for (int i = 0; i < kFrames; ++i) {
            if (i == 0 && onFor > 0) {
                options.mesh.view = coney::debug::MeshShading::Material;
                options.mesh.viewScope = coney::debug::MeshScope::Everything;
            }
            if (i == onFor) {
                options.mesh = coney::debug::MeshViewOptions{};
            }
            yard.run(1);
        }
        yard.engine->setFrameTap({});
        return hashes;
    };
    coney::debug::DebugDrawOptions none;
    const std::vector<std::uint64_t> plain = frames(nullptr, none, 0);
    if (skipped == Skip::NoDisc) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    if (skipped == Skip::NoWindow) {
        SKIP("no OpenGL window: the pixels cannot be compared here");
    }
    REQUIRE(plain.size() >= static_cast<std::size_t>(kFrames));

    coney::debug::DebugDrawOptions off;
    const std::vector<std::uint64_t> withViews = frames(std::make_shared<coney::platform::MeshViews>(off), off, 0);
    CHECK(withViews == plain);

    // Switched on for the first frames, the frames differ (the hook reaches what is drawn); once off again they are
    // the plain run's, which `--mesh-view ...,frames=N` relies on.
    coney::debug::DebugDrawOptions on;
    const std::vector<std::uint64_t> flat = frames(std::make_shared<coney::platform::MeshViews>(on), on, kOnFrames);
    REQUIRE(flat.size() == plain.size());
    std::size_t differing = 0;
    for (int i = 0; i < kOnFrames; ++i) {
        differing += flat[static_cast<std::size_t>(i)] != plain[static_cast<std::size_t>(i)] ? 1 : 0;
    }
    CHECK(differing > 0);
    for (std::size_t i = static_cast<std::size_t>(kOnFrames); i < plain.size(); ++i) {
        CHECK(flat[i] == plain[i]);
    }
    std::printf("frame identity: %zu frames equal with the views off; %zu of the first %d differ with a flat view on\n",
                plain.size(), differing, kOnFrames);
}
