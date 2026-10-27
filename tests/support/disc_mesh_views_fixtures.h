// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the disc tests of the mesh views share (docs/guides/debug-menu.md#mesh-views): a hook that records what the play
// mode hands it, the Entity inspector's Mesh group read back as numbers, the sandbox's fight yard on the Null renderer
// or a hidden OpenGL window, and a level of the game session played until the player has his pad. They print counts
// only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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
#include "debug/entity_inspect.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_start.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/mesh_view_hook.h"
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

namespace coney::test::mesh_views {

// How long the level may take to give the player his pad: 60 s of frames.
inline constexpr std::uint64_t kLimit = 1800;

// A pad script that taps cross every second for the first 20 s of play: it skips the level's opening scene.
inline constexpr const char* kSkipScene = "30 tap cross\n60 tap cross\n90 tap cross\n120 tap cross\n150 tap cross\n"
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
inline std::vector<std::size_t> numbersIn(std::string_view text) {
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
inline std::optional<MeshFacts> meshFactsOf(const coney::debug::EntityInspect& inspect, EntityId id) {
    const std::vector<coney::debug::WatchGroup> groups = inspect.describe(id);
    const std::optional<std::string> calls = coney::debug::watchValue(groups, "Mesh", "Draw calls last frame");
    if (!calls) {
        return std::nullopt;
    }
    const std::vector<std::size_t> numbers = numbersIn(*calls);
    REQUIRE(numbers.size() == 3);
    const std::optional<std::string> triangles = coney::debug::watchValue(groups, "Mesh", "Triangles");
    REQUIRE(triangles.has_value());
    // Read through `value_or` so that the check above is the only way the value is missing.
    const std::vector<std::size_t> triangleNumbers = numbersIn(triangles.value_or(""));
    REQUIRE_FALSE(triangleNumbers.empty());
    return MeshFacts{.drawCalls = numbers[0],
                     .materials = numbers[1],
                     .passes = numbers[2],
                     .triangles = triangleNumbers.front(),
                     .skinnedOn = coney::debug::watchValue(groups, "Mesh", "Skinned on").value_or("")};
}

// The ids of the humans the inspector lists, player 1 first.
inline std::vector<EntityId> humanIds(const coney::debug::EntityInspect& inspect) {
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
inline coney::platform::WindowDesc testWindow() {
    coney::platform::WindowDesc desc;
    desc.title = "Coney mesh views check";
    desc.width = 320;
    desc.height = 240;
    desc.hidden = true;
    desc.logicalFrame = true;
    return desc;
}

// Whether the machine can open an OpenGL window to draw in (a display and a context; CI has none).
inline bool windowAvailable() {
    const coney::platform::WindowDesc desc = testWindow();
    return coney::platform::RenderEngine::start(coney::platform::RenderBackend::OpenGl, desc).has_value();
}

// Opens the yard from CONEY_DISC, on the Null renderer or (`window`) a hidden OpenGL window, with two fighters ahead of
// the player so there are humans to look at. The Null renderer draws nothing: the play mode's frames then step the
// world and ask the views' hook, and no model is drawn.
inline Opened openYard(Yard& yard, bool window) {
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
inline std::vector<std::size_t> passesOfHumans(const coney::platform::PlayLevelControls& controls) {
    std::vector<std::size_t> passes;
    for (const EntityId id : humanIds(*controls.inspect())) {
        passes.push_back(meshFactsOf(*controls.inspect(), id).value_or(MeshFacts{}).passes);
    }
    return passes;
}

// A level of the game session, started and played until the opening scene is skipped and the player has his pad, on
// whatever renderer the `DiscSession` was made with (Null, or a hidden window); false (and the run unfinished) when it
// does not get there.
inline bool playLevel(coney::test::DiscSession& run, std::string_view level) {
    run.session().startAtLevel(level, 1);
    if (!run.runUntilPlay()) {
        return false;
    }
    return run.runUntil(
        [&run] { return run.session().play() != nullptr && run.session().play()->player().padControlled(); }, kLimit);
}

} // namespace coney::test::mesh_views
