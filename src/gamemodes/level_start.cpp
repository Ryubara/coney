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
#include "scripting/hud_bindings.h"
#include "scripting/lua_value.h"
#include "scripting/script_bindings.h"

namespace coney {

namespace {

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

LevelScripts::LevelScripts(const script::ScriptSource& source, std::string_view level, int checkpoint,
                           const std::function<void(std::string_view)>& log, const LevelScriptOptions& options)
    : m_context{&m_state, &m_strings,  &m_host,  &m_recorded,      &m_humans,      &m_flags,       &m_rumbleData,
                nullptr,  &m_messages, &m_boxes, &m_animCallbacks, &m_objectTypes, &m_spawnRecords},
      m_scripts(
          source,
          [this](script::ScriptSystem& system, script::LuaVm& vm) { script::installBindings(system, vm, m_context); },
          log) {
    // The HUD the scripts' HUD bindings act on, before the first Lua state (below).
    m_context.hud = &m_hud;
    m_hud.setServices(script::hudServicesOf(m_context));
    if (options.randomTable.size() == GameRandom::kTableSize) {
        m_state.random.setTable(options.randomTable);
    }
    if (options.rumble) {
        m_state.rumble = *options.rumble;
    }

    // The legal screen's preloads, in the first Lua state: they fill the level table.
    m_scripts.create();
    m_scripts.runFiles(script::kEnumPreloadScripts);
    m_scripts.runFiles(script::kConfigPreloadScripts);

    // An arena run alone gets the Rumble menu's default set-up: its chunks need the `RM_*` names `global.lua` defines.
    if (!options.rumble && options.rumbleArena) {
        m_scripts.runFile(script::kGlobalScript);
        const gui::RumbleMenuServices services{
            .state = &m_state,
            .data = &m_rumbleData,
            .strings = &m_strings,
            .runChunk = [this](std::string_view chunk) { m_scripts.runFile(chunk); }};
        if (!gui::rumbleMenuDefaults(services, *options.rumbleArena)) {
            m_scripts.log("rumble: the menu's chunks list no mode or gang; the set-up stays empty");
        }
    }

    // The front end's unload makes a fresh state, and runNextMission sets the checkpoint and chooses the level by
    // name. A level the table does not list gets index 0 (Coney's choice: the menus never ask for one).
    m_scripts.create();
    m_state.checkPoint = checkpoint;
    m_state.currentLevel = m_state.levels.find(level).value_or(0);
}

LevelScriptRun runLevelScriptAlone(const script::ScriptSource& source, std::string_view level, int checkpoint,
                                   const std::function<void(std::string_view)>& log,
                                   const LevelScriptOptions& options) {
    LevelScripts prepared(source, level, checkpoint, log, options);
    script::ScriptSystem& scripts = prepared.scripts();
    CreatedHumans& humans = prepared.humans();

    LevelScriptRun run;
    run.start = runLevelScript(scripts, prepared.state(), humans, prepared.flags(), level);

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
    run.flags = prepared.flags().all().size();
    run.recorded = std::move(prepared.recorded());
    return run;
}

} // namespace coney
