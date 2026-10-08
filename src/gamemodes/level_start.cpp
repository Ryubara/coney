// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_start.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "effects/particles.h"
#include "gui/global_strings.h"
#include "gui/rumble_mode_gui/rumble_data.h"
#include "gui/rumble_mode_gui/rumble_menu.h"
#include "human/locomotion.h"
#include "scripting/hud_bindings.h"
#include "scripting/lua_value.h"
#include "scripting/script_bindings.h"
#include "world_objects/placed_objects_file.h"

namespace coney {

namespace {

// The script frames runLevelScriptAlone() runs after the start: one second of the fixed 1/30 s step.
constexpr std::uint64_t kSettleSteps = 30;

// Reads `<level>_objs.txt` through the scripts' source and adds its objects to `records` and its emitters to
// `particles` (when given: a particle system of the line's name at its pose, its fields past the pose unused, and no
// handle, as the loader keeps none), logging the counts, or why there were none.
void loadPlacedObjects(script::ScriptSystem& scripts, world_objects::SpawnRecords& records, std::string_view level,
                       effects::ParticleSystems* particles) {
    const std::string name = std::format("{}_objs.txt", level);
    const auto bytes = scripts.readFile(name);
    if (!bytes) {
        scripts.log(std::format("level: no {}: {}", name, bytes.error().message));
        return;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    const auto objects = world_objects::parsePlacedObjects(text);
    if (!objects) {
        scripts.log(std::format("level: {}: {}", name, objects.error().message));
        return;
    }
    std::size_t emitters = 0;
    const std::size_t added = world_objects::addPlacedObjects(
        *objects, records, [&scripts] { return scripts.nextObjectHandle(); },
        [particles, &emitters](const world_objects::PlacedObject& object) {
            if (particles != nullptr &&
                particles->spawn(object.name, anim::Vec3{object.position[0], object.position[1], object.position[2]},
                                 anim::Quat{object.rotation[0], object.rotation[1], object.rotation[2],
                                            object.rotation[3]}) != nullptr) {
                ++emitters;
            }
        });
    scripts.log(
        std::format("level: {} placed {} objects and {} emitters of {} lines", name, added, emitters, objects->size()));
}

} // namespace

LevelStart runLevelScript(script::ScriptSystem& scripts, GameState& state, CreatedHumans& humans,
                          world_objects::WorldFlags& flags, std::string_view level,
                          world_objects::SpawnRecords* records, effects::ParticleSystems* particles) {
    // The humans and flags of the level before are gone: the original's unload frees every slot and the flag pool.
    humans.clear();
    flags.clear();
    state.startGameCallback.clear();
    // InitLevel's reset of the Warrior commands (`0x00418c68`): each player's last command is follow, all seven are
    // enabled and the menu is unlocked (docs/research/ai.md#warrior-follow).
    for (std::size_t player = 0; player < kWarriorPlayers; ++player) {
        state.characters.lastWarriorCommand.at(player) = 0;
        state.characters.warriorCommands.at(player).fill(true);
        state.story.menuLocked.at(player) = false;
    }
    scripts.enterLevel(level);

    // Step 7: the level's placed objects, into the spawn records.
    if (records != nullptr) {
        loadPlacedObjects(scripts, *records, level, particles);
    }

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

    // Steps 9-10: the preload services the file manager, so the checkpoint scripts the level asked for with
    // preLoadFile arrive and run here, before the start (docs/research/level-loading.md, inferred).
    scripts.servicePreloads(true);

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

// The player's turning as the preload scripts configure it (`CfgSetTurnRates`, `CfgTurnRate` in config_preload2.lua,
// recorded by their stubs): the values the original plays with (docs/research/characters.md#movement-constants).
void applyTurnConfig(const script::RecordedCalls& recorded) {
    // A recorded argument as a float; nil and strings count as nothing.
    const auto numberAt = [](const std::vector<script::Value>& args, std::size_t i) -> std::optional<float> {
        if (i >= args.size()) {
            return std::nullopt;
        }
        const std::optional<double> number = args[i].number();
        return number ? std::optional<float>(static_cast<float>(*number)) : std::nullopt;
    };
    for (const std::vector<script::Value>& args : recorded.calls("CfgSetTurnRates")) {
        std::vector<float> degrees;
        degrees.reserve(args.size());
        for (std::size_t i = 0; i < args.size(); ++i) {
            degrees.push_back(numberAt(args, i).value_or(-1.0F)); // -1 is out of range: the old value stays
        }
        human::setPlayerTurnRates(degrees);
    }
    for (const std::vector<script::Value>& args : recorded.calls("CfgTurnRate")) {
        const std::optional<float> ease = numberAt(args, 2);
        const std::optional<float> carry = numberAt(args, 3);
        if (ease && carry) {
            human::setTurnEase(*ease, *carry);
        }
    }
}

void QuietBindingHost::launchMissionComplete(int kind) {
    m_complete = kind;
    m_missionComplete = m_missionComplete || !m_unlocking;
    if (m_log) {
        m_log(std::format("level: mission complete (kind {})\n", kind));
    }
}

void QuietBindingHost::launchMissionFailed(std::string_view reason) {
    m_failed = std::string(reason);
    if (m_log) {
        m_log(std::format("level: mission failed: {}\n", reason));
    }
}

LevelScripts::LevelScripts(const script::ScriptSource& source, std::string_view level, int checkpoint,
                           const std::function<void(std::string_view)>& log, const LevelScriptOptions& options)
    : m_context{&m_state, &m_strings,  &m_host,  &m_recorded,      &m_humans,      &m_flags,       &m_rumbleData,
                nullptr,  &m_messages, &m_boxes, &m_animCallbacks, &m_objectTypes, &m_spawnRecords},
      m_scripts(
          source,
          [this](script::ScriptSystem& system, script::LuaVm& vm) { script::installBindings(system, vm, m_context); },
          log) {
    m_host.setLog(log);
    // The HUD the scripts' HUD bindings act on and the sound the preloads configure, before the first Lua state.
    m_context.hud = &m_hud;
    m_context.sound = options.sound;
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

bool LevelScripts::completeMission() {
    m_host.clearMissionComplete();
    m_host.setUnlocking(true);
    const bool called = m_scripts.call("UnlockAndLoad");
    m_host.setUnlocking(false);
    // Both players' mission money goes to the bank (mode 0xb, `0x0041e398`).
    for (int player = 0; player < Inventory::kPlayers; ++player) {
        const int money = m_state.player.inventory.count(player, item::kMoney);
        if (money > 0) {
            m_state.saved.addToBank(static_cast<std::uint32_t>(money));
        }
    }
    return called;
}

void LevelScripts::carryProgressTo(LevelScripts& next) const {
    next.m_state.saved = m_state.saved;
    next.m_state.luaSaveFloats = m_state.luaSaveFloats;
}

LevelScriptRun runLevelScriptAlone(const script::ScriptSource& source, std::string_view level, int checkpoint,
                                   const std::function<void(std::string_view)>& log,
                                   const LevelScriptOptions& options) {
    LevelScripts prepared(source, level, checkpoint, log, options);
    script::ScriptSystem& scripts = prepared.scripts();
    CreatedHumans& humans = prepared.humans();

    LevelScriptRun run;
    run.start = runLevelScript(scripts, prepared.state(), humans, prepared.flags(), level, &prepared.spawnRecords());

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
