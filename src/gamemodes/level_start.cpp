// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_start.h"

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/assert.h"
#include "gui/global_strings.h"
#include "gui/rumble_mode_gui/rumble_data.h"
#include "gui/rumble_mode_gui/rumble_menu.h"
#include "scripting/lua_value.h"
#include "scripting/script_bindings.h"

namespace coney {

namespace {

// What the bindings ask of the game while a level's scripts run alone: there are no menus, modes or audio, so every
// request is dropped. A level script that asks for a level (`MenuLoadLevel`) or a movie is not acted on.
class QuietHost final : public script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
};

// The script frames runLevelScriptAlone() runs after the start: one second of the fixed 1/30 s step.
constexpr std::uint64_t kSettleSteps = 30;

} // namespace

LevelStart runLevelScript(script::ScriptSystem& scripts, GameState& state, CreatedHumans& humans,
                          world_objects::WorldFlags& flags, std::string_view level) {
    // The humans and flags of the level before are gone: the original's unload frees every slot and the flag pool.
    humans.clear();
    flags.clear();
    state.startGameCallback.clear();
    scripts.enterLevel(level);

    // InitLevel's own two flags, at the origin facing 0, made through AddFlag so their handles come from the same
    // counter as every other world object's.
    for (const std::string_view name : {kCrimeSceneFlag, kGangCallFlag}) {
        auto origin = std::make_shared<script::Table>();
        for (int axis = 1; axis <= 3; ++axis) {
            // A number key into a fresh table cannot fail.
            const bool stored = origin->set(script::Value(static_cast<double>(axis)), script::Value(0.0)).has_value();
            CONEY_ASSERT(stored);
        }
        const std::array<script::Value, 5> args{script::Value(std::string(name)), script::Value(std::move(origin)),
                                                script::Value(0.0), script::Value(0.0), script::Value(0.0)};
        scripts.call("AddFlag", args);
    }

    // The start callback the script set (an arena's DoRules, which places the players), called once.
    if (!state.startGameCallback.empty()) {
        const std::string callback = std::exchange(state.startGameCallback, std::string{});
        scripts.call(callback);
    }

    // Player 1 as the scripts made him. A creation without a readable position cannot be placed, so it counts as none.
    LevelStart start;
    start.level = std::string(level);
    start.checkpoint = static_cast<int>(std::trunc(state.checkPoint));
    if (const HumanCreation* player = humans.player(1); player != nullptr && (player->position || player->teleported)) {
        start.player = *player;
    }
    return start;
}

LevelScriptRun runLevelScriptAlone(const script::ScriptSource& source, std::string_view level, int checkpoint,
                                   const std::function<void(std::string_view)>& log,
                                   const LevelScriptOptions& options) {
    // What the bindings work on: a game state, the strings and configuration the preloads fill, and the humans.
    GameState state;
    gui::GlobalStrings strings;
    script::RecordedCalls recorded;
    CreatedHumans humans;
    world_objects::WorldFlags flags;
    QuietHost host;
    gui::RumbleData rumbleData;
    const script::BindingContext context{&state, &strings, &host, &recorded, &humans, &flags, &rumbleData};
    if (options.randomTable.size() == GameRandom::kTableSize) {
        state.random.setTable(options.randomTable);
    }
    if (options.rumble) {
        state.rumble = *options.rumble;
    }
    script::ScriptSystem scripts(
        source,
        [&context](script::ScriptSystem& system, script::LuaVm& vm) { script::installBindings(system, vm, context); },
        log);

    // The legal screen's preloads, in the first Lua state: they fill the level table.
    scripts.create();
    scripts.runFiles(script::kEnumPreloadScripts);
    scripts.runFiles(script::kConfigPreloadScripts);

    // An arena run alone gets the Rumble menu's default set-up: its chunks need the `RM_*` names `global.lua` defines.
    if (!options.rumble && options.rumbleArena) {
        scripts.runFile(script::kGlobalScript);
        const gui::RumbleMenuServices services{
            .state = &state, .data = &rumbleData, .strings = &strings, .runChunk = [&scripts](std::string_view chunk) {
                scripts.runFile(chunk);
            }};
        if (!gui::rumbleMenuDefaults(services, *options.rumbleArena)) {
            scripts.log("rumble: the menu's chunks list no mode or gang; the set-up stays empty");
        }
    }

    // The front end's unload makes a fresh state, and runNextMission sets the checkpoint and chooses the level by
    // name. A level the table does not list gets index 0 (Coney's choice: the menus never ask for one).
    scripts.create();
    state.checkPoint = checkpoint;
    state.currentLevel = state.levels.find(level).value_or(0);

    LevelScriptRun run;
    run.start = runLevelScript(scripts, state, humans, flags, level);

    // The first second of play's script frames, as gameplay would run them, so what the start schedules (the hub's
    // walk, 100 ms in) happens; player 1 is then where those calls left him.
    for (std::uint64_t step = 1; step <= kSettleSteps; ++step) {
        const std::uint64_t nowMs = step * 1000 / kSettleSteps;
        scripts.setTime(nowMs);
        scripts.update(nowMs, 1.0 / static_cast<double>(kSettleSteps));
    }
    if (const HumanCreation* player = humans.player(1); player != nullptr && run.start.player) {
        run.start.player = *player;
    }
    run.scriptErrors = scripts.errors();
    run.skippedCalls = scripts.skippedCalls();
    run.humans = humans.all().size();
    run.flags = flags.all().size();
    run.recorded = std::move(recorded);
    return run;
}

} // namespace coney
