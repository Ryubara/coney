// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/script_bindings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "characters/character_class.h"
#include "core/assert.h"
#include "scenes/scene_player.h"
#include "scripting/ai_bindings.h"
#include "scripting/anim_callbacks.h"
#include "scripting/arena_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/camera_bindings.h"
#include "scripting/car_bindings.h"
#include "scripting/config_strings.h"
#include "scripting/effects_bindings.h"
#include "scripting/gang_bindings.h"
#include "scripting/hub_bindings.h"
#include "scripting/hub_world_bindings.h"
#include "scripting/hud_bindings.h"
#include "scripting/human_bindings.h"
#include "scripting/level_bindings.h"
#include "scripting/lighting_bindings.h"
#include "scripting/object_bindings.h"
#include "scripting/player_bindings.h"
#include "scripting/rumble_bindings.h"
#include "scripting/rumble_match_bindings.h"
#include "scripting/scene_bindings.h"
#include "scripting/sound_bindings.h"
#include "scripting/spawn_bindings.h"
#include "scripting/story_bindings.h"
#include "scripting/story_effects_bindings.h"
#include "scripting/trigger_bindings.h"
#include "scripting/world_bindings.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

namespace coney::script {

namespace {

// The value of the global `NilHandle` (Coney's choice, below): what a binding returns for no object.
constexpr double kNilHandle = 0.0;

// The handles stub bindings give out, counting from 1 for each state.
struct HandleCounter {
    double next = 1;
};

// What a real binding's factory gets: the script system, the context and the state's handle counter.
struct Factory {
    ScriptSystem* scripts;
    const BindingContext* context;
    std::shared_ptr<HandleCounter> handles;
};

// Makes one real or routed binding.
using MakeBinding = NativeFunction (*)(const Factory& factory);

// ---- Real bindings ----

// `GetPlatform()`: always 1 on the PS2.
// @orig 0x00357998 GetPlatform (unknown)
NativeFunction makeGetPlatform(const Factory& /*factory*/) {
    return [](std::span<const Value>) { return binding::number(kPlatformValue); };
}

// `isRelease()`: always true.
// @orig 0x00357990 isRelease (unknown)
NativeFunction makeIsRelease(const Factory& /*factory*/) {
    return [](std::span<const Value>) { return binding::boolean(true); };
}

// `GetLanguage()`: the game state's language, 0 English to 4 German.
// @orig 0x0041d7f0 GetLanguage (unknown)
NativeFunction makeGetLanguage(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value>) {
        return binding::number(static_cast<double>(state->language));
    };
}

// `GetCurrentLevelIndex()`: the current level record's index; 0, the front end, at start-up.
// @orig 0x0041d718 GetCurrentLevelIndex (unknown)
NativeFunction makeGetCurrentLevelIndex(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value>) {
        return binding::number(static_cast<double>(state->currentLevel));
    };
}

// `GetLevelId(i)`: record i's level number; 0 for an index with no record (Coney's choice: the original reads a
// cleared record).
// @orig 0x0041d6f0 GetLevelId (unknown)
NativeFunction makeGetLevelId(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value> args) {
        const double index = binding::number(args, 0);
        const LevelRecord* record = index >= 0.0 && index < static_cast<double>(LevelTable::kCapacity)
                                        ? state->levels.at(static_cast<std::size_t>(index))
                                        : nullptr;
        return binding::number(record != nullptr ? record->number : 0.0);
    };
}

// `GetDifficulty()`.
// @orig 0x0041d800 GetDifficulty (unknown)
NativeFunction makeGetDifficulty(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value>) { return binding::number(state->difficulty); };
}

// `GetProfileDifficulty()`.
// @orig 0x0041d820 GetProfileDifficulty (unknown)
NativeFunction makeGetProfileDifficulty(const Factory& factory) {
    return
        [state = factory.context->state](std::span<const Value>) { return binding::number(state->profileDifficulty); };
}

// `GetCheckPoint()`: the level's current section.
// @orig 0x0041abe8 GetCheckPoint (unknown)
NativeFunction makeGetCheckPoint(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value>) { return binding::number(state->checkPoint); };
}

// `SetCheckPoint(n)`: sets the section GetCheckPoint reads and takes the checkpoint copy of the inventories and the
// statistics a restart puts back.
// @orig 0x0041ce98 GameState_SetCheckPoint (unknown)
NativeFunction makeSetCheckPoint(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value> args) {
        state->checkPoint = binding::number(args, 0);
        state->player.saveCheckpoint();
        return binding::none();
    };
}

// `ToInt(x)`: x truncated towards zero.
// @orig 0x0036d938 ToInt (unknown)
NativeFunction makeToInt(const Factory& /*factory*/) {
    return [](std::span<const Value> args) { return binding::number(std::trunc(binding::number(args, 0))); };
}

// `doFile(name)`: runs `name.lua` now, in this state.
// @orig 0x003579a0 doFile (unknown)
NativeFunction makeDoFile(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        scripts->runFile(std::format("{}.lua", binding::string(args, 0)));
        return binding::none();
    };
}

// `preLoadFile(name, callback)`: the original asks for `name.lua` without waiting; when it arrives it runs the chunk,
// then, when a callback name was given, calls that function with no arguments (docs/research/scripting.md#open-
// questions, `RegisterUpdate`). Coney's reads are synchronous, so both happen at once, inside the call.
// @orig 0x00357a68 preLoadFile (unknown)
// @orig 0x00356d00 ScriptSystem_PreloadDone (unknown)
NativeFunction makePreLoadFile(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        const bool ran = scripts->runFile(std::format("{}.lua", binding::string(args, 0)));
        if (const std::string callback = binding::string(args, 1); ran && !callback.empty()) {
            scripts->call(callback);
        }
        return binding::none();
    };
}

// `ScheduleFunc(name, ms)`: calls `name` ms milliseconds of game time from now.
// @orig 0x003863d8 ScheduleFunc (unknown)
NativeFunction makeScheduleFunc(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        scripts->schedule(binding::string(args, 0),
                          static_cast<std::uint64_t>(std::max(0.0, binding::number(args, 1))));
        return binding::none();
    };
}

// `ScheduleFuncArg1(name, n, ms)`: the same with one number argument, which comes before the delay; level95's
// `events.ChatEvent` relies on this order (docs/research/scripting.md#errors-in-a-fresh-state).
// @orig 0x00386410 ScheduleFuncArg1 (unknown)
NativeFunction makeScheduleFuncArg1(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        const std::array<double, 1> callArgs{binding::number(args, 1)};
        scripts->schedule(binding::string(args, 0), static_cast<std::uint64_t>(std::max(0.0, binding::number(args, 2))),
                          callArgs);
        return binding::none();
    };
}

// `FlushScheduledFuncs(name)`: drops the scheduled calls of `name`, or all of them without a name.
// @orig 0x00386450 FlushScheduledFuncs (unknown)
NativeFunction makeFlushScheduledFuncs(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        scripts->flushScheduled(binding::string(args, 0));
        return binding::none();
    };
}

// `GetGameTime()`: the game time in milliseconds, the clock the schedule counts on.
// @orig 0x0036e050 GetGameTime (unknown)
NativeFunction makeGetGameTime(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value>) {
        return binding::number(static_cast<double>(scripts->now()));
    };
}

// `gc()`: collect garbage now; Coney's VM has none to collect.
// @orig 0x00386370 gc (unknown)
NativeFunction makeGc(const Factory& /*factory*/) {
    return [](std::span<const Value>) { return binding::none(); };
}

// `ShowProfileManager(onRumble, onStartGame)`: shows the menus with the two callbacks.
// @orig 0x0036eef8 ShowProfileManager_Binding (unknown)
NativeFunction makeShowProfileManager(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->showProfileManager(binding::string(args, 0), binding::string(args, 1));
        return binding::none();
    };
}

// `MenuLoadLevel(name)`: chooses the level to start.
// @orig 0x0036df48 MenuLoadLevel (unknown)
NativeFunction makeMenuLoadLevel(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->menuLoadLevel(binding::string(args, 0));
        return binding::none();
    };
}

// `ScreenQueueEffect(type, seconds)`: queues a fade (0 in, 1 out).
NativeFunction makeScreenQueueEffect(const Factory& factory) {
    return [context = factory.context](std::span<const Value> args) {
        const int type = static_cast<int>(binding::number(args, 0));
        const double seconds = binding::number(args, 1);
        context->host->queueScreenEffect(type, seconds);
        // In play the screen's effects are the scenes' host's (the play mode's stage), whose fades a scene's own use.
        if (context->scenes != nullptr) {
            context->scenes->queueScreenEffect(type, static_cast<float>(seconds));
        }
        return binding::none();
    };
}

// `CfgLevelName(...)`: one level record from its 18 arguments (LevelRecord gives Coney's reading of their order).
// @orig 0x0036b220 CfgLevelName (unknown)
NativeFunction makeCfgLevelName(const Factory& factory) {
    return [state = factory.context->state, scripts = factory.scripts](std::span<const Value> args) {
        LevelRecord record;
        record.id = binding::number(args, 0);
        record.name = binding::string(args, 1);
        record.secondName = binding::string(args, 2);
        record.worldName = binding::string(args, 3);
        record.fourthName = binding::string(args, 4);
        record.number = binding::number(args, 5);
        for (std::size_t i = 0; i < record.values.size(); ++i) {
            record.values.at(i) = binding::number(args, 6 + i);
        }
        if (!state->levels.set(std::move(record))) {
            scripts->log(std::format("CfgLevelName: record index {} is outside the level table; ignored",
                                     binding::number(args, 0)));
        }
        return binding::none();
    };
}

// `random(low, high)`: a whole number in [low, high] from the game's generator, the table walked by one index for the
// whole session (GameRandom, in the game state, so every Lua state shares it).
// @orig 0x00386488 random (unknown)
NativeFunction makeRandom(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value> args) {
        const auto low = static_cast<std::int32_t>(std::trunc(binding::number(args, 0)));
        const auto high = static_cast<std::int32_t>(std::trunc(binding::number(args, 1)));
        return binding::number(static_cast<double>(state->random.range(low, high)));
    };
}

// The model-name argument of type `type`'s `CfgChar` call among the recorded configuration; nothing when there is none.
std::optional<std::string> recordedCfgCharModel(const RecordedCalls* recorded, int type) {
    if (recorded == nullptr) {
        return std::nullopt;
    }
    for (const std::vector<Value>& call : recorded->calls("CfgChar")) {
        if (call.empty() || call[0].number() != static_cast<double>(type) ||
            call.size() <= characters::kCfgCharModelArgument) {
            continue;
        }
        if (const std::optional<std::string_view> model = call[characters::kCfgCharModelArgument].string()) {
            return std::string(*model);
        }
    }
    return std::nullopt;
}

// The current level's number (its record's `+0x04`); 0 when the table has no record for it.
int currentLevelNumber(const GameState& state) {
    const LevelRecord* record = state.levels.at(state.currentLevel);
    return record != nullptr ? static_cast<int>(record->number) : 0;
}

// `HuCreate(name, type, {x, y, z}, heading, unused, player, gang, flag)`: makes a human and returns its handle, or
// `NilHandle` when every slot is taken. The human is kept in the context's CreatedHumans and told to the AI host,
// which (in play) makes it a character with a brain once the level has loaded (ai::ScriptedBrains). Coney's choices:
// the position is not snapped to the ground here (no collision is loaded while the script runs) and so not written back
// into the table; the play mode snaps the player the same way when it places him
// (docs/research/characters.md#creation). The gang is kept for the AI (nil is none); the unused string and the flag are
// not. The model the type is drawn as is resolved here from the recorded `CfgChar` calls (characters::modelNameFor(),
// docs/research/characters.md#type-to-model); the play mode loads it.
// @orig 0x00358428 HuCreate (unknown)
// @orig 0x00233d60 Human_Create (unknown)
NativeFunction makeHuCreate(const Factory& factory) {
    return [context = factory.context, handles = factory.handles](std::span<const Value> args) {
        HumanCreation human;
        human.name = binding::string(args, 0);
        human.type = static_cast<int>(std::trunc(binding::number(args, 1)));
        human.position = binding::position(args, 2);
        human.headingDegrees = static_cast<float>(binding::number(args, 3));
        human.playerIndex = static_cast<int>(std::trunc(binding::number(args, 5)));
        human.gang = args.size() > 6 && !args[6].isNil() ? static_cast<int>(std::trunc(binding::number(args, 6))) : -1;
        human.model =
            characters::modelNameFor(human.type, human.playerIndex, currentLevelNumber(*context->state),
                                     [context](int type) { return recordedCfgCharModel(context->recorded, type); })
                .value_or(std::string{});
        human.handle = handles->next;
        CreatedHumans* humans = context->humans;
        if (humans != nullptr && !humans->add(human)) {
            return binding::number(kNilHandle);
        }
        handles->next += 1;
        // The AI host makes the human in the world (or once the level's characters are loaded).
        if (context->ai != nullptr) {
            context->ai->humanCreated(human);
        }
        return binding::number(human.handle);
    };
}

// `GangCreate(kind, name)`: the new gang's id from the AI host, or -1 when it has no free slot or the name is taken.
// Without a host (a level script run on its own) the stubs' next handle, as before gangs were real, so the scripts'
// later handles stay the same.
// @orig 0x00373148 GangCreate (unknown)
NativeFunction makeGangCreate(const Factory& factory) {
    return [context = factory.context, handles = factory.handles](std::span<const Value> args) {
        if (AiBindingHost* host = context->ai; host != nullptr) {
            return binding::number(
                host->gangCreate(static_cast<int>(std::trunc(binding::number(args, 0))), binding::string(args, 1)));
        }
        const double handle = handles->next;
        handles->next += 1;
        return binding::number(handle);
    };
}

// `HUDLaunchMissionComplete(kind)`: shows the mission-complete mode with `kind` (runNextMission passes 4).
// @orig 0x0036f218 HUDLaunchMissionComplete (unknown)
NativeFunction makeHudLaunchMissionComplete(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->launchMissionComplete(static_cast<int>(std::trunc(binding::number(args, 0))));
        return binding::none();
    };
}

// `SSMC_StartLoadSequence()`: reads the profiles again (RELOAD PROFILES; docs/research/save.md#mode-6).
// @orig 0x0037b6c8 SSMC_StartLoadSequence_Binding (unknown)
NativeFunction makeStartLoadSequence(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value>) {
        host->startLoadSequence();
        return binding::none();
    };
}

// `SSMC_StartDeleteSequence()`: writes the deleted profiles out, after PM_Delete (docs/research/save.md#mode-6).
// @orig 0x0037b6e8 SSMC_StartDeleteSequence_Binding (unknown)
NativeFunction makeStartDeleteSequence(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value>) {
        host->startDeleteSequence();
        return binding::none();
    };
}

// `HUDLaunchMissionFailed(reason)`: shows the mission-failed mode with the failure text.
// @orig 0x0036f130 HUDLaunchMissionFailed (unknown)
NativeFunction makeHudLaunchMissionFailed(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->launchMissionFailed(binding::string(args, 0));
        return binding::none();
    };
}

// ---- Coney's scene stand-in (docs/research/scenes.md): with no scene system, a scene loads and ends at once ----

// Schedules the Lua function named by string argument `i` (none for nil or a number) with the scene id, to run at the
// scripts' next update: the original calls it later too (on the file's arrival, at the scene's end), never inside the
// binding, and `SuperRunScene` stores its table only after `ScenePreload` returns.
void scheduleSceneCallback(ScriptSystem& scripts, std::span<const Value> args, std::size_t i, double scene) {
    if (i >= args.size() || args[i].type() != Value::Type::String) {
        return;
    }
    const std::array<double, 1> callArgs{scene};
    scripts.schedule(binding::string(args, i), 0, callArgs);
}

// `ScenePreload(name, onLoaded)`: a new handle as the scene's id, and **Coney's stand-in** for the load: `onLoaded` is
// called with the id at the next script update (docs/research/scenes.md#loading).
NativeFunction makeStandInScenePreload(const Factory& factory) {
    return [scripts = factory.scripts, handles = factory.handles](std::span<const Value> args) {
        const double scene = handles->next;
        handles->next += 1;
        scheduleSceneCallback(*scripts, args, 1, scene);
        return binding::number(scene);
    };
}

// `ScenePlayCinematic(scene, delay, onEnd, ...)`, `ScenePlayAnimation` and `ScenePlayFixedScene` (`onEnd` third in
// each): true, and **Coney's stand-in** for the scene: it ends at once, its end function called with the scene id at
// the next script update (docs/research/scenes.md#ending). A looping scene ends too.
NativeFunction makeStandInScenePlay(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        scheduleSceneCallback(*scripts, args, 2, binding::number(args, 0));
        return binding::boolean(true);
    };
}

// ---- Routed bindings: handed to the host's stand-ins ----

// `PlayMovie(name)`.
NativeFunction makePlayMovie(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->playMovie(binding::string(args, 0));
        return binding::none();
    };
}

// `ShowRumbleModeInterface(onCancel, onStart, n)`.
NativeFunction makeShowRumbleModeInterface(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->showRumbleModeInterface(binding::string(args, 0), binding::string(args, 1), binding::number(args, 2));
        return binding::none();
    };
}

// The real and routed bindings' makers, by name.
struct Maker {
    std::string_view name;
    MakeBinding make;
};
constexpr std::array kMakers{
    Maker{"CfgLevelName", makeCfgLevelName},
    Maker{"FlushScheduledFuncs", makeFlushScheduledFuncs},
    Maker{"GangCreate", makeGangCreate},
    Maker{"GetCheckPoint", makeGetCheckPoint},
    Maker{"GetCurrentLevelIndex", makeGetCurrentLevelIndex},
    Maker{"GetDifficulty", makeGetDifficulty},
    Maker{"GetGameTime", makeGetGameTime},
    Maker{"GetLanguage", makeGetLanguage},
    Maker{"GetLevelId", makeGetLevelId},
    Maker{"GetPlatform", makeGetPlatform},
    Maker{"GetProfileDifficulty", makeGetProfileDifficulty},
    Maker{"HUDLaunchMissionComplete", makeHudLaunchMissionComplete},
    Maker{"HUDLaunchMissionFailed", makeHudLaunchMissionFailed},
    Maker{"HuCreate", makeHuCreate},
    Maker{"MenuLoadLevel", makeMenuLoadLevel},
    Maker{"PlayMovie", makePlayMovie},
    Maker{"SSMC_StartDeleteSequence", makeStartDeleteSequence},
    Maker{"SSMC_StartLoadSequence", makeStartLoadSequence},
    Maker{"ScheduleFunc", makeScheduleFunc},
    Maker{"ScheduleFuncArg1", makeScheduleFuncArg1},
    Maker{"ScreenQueueEffect", makeScreenQueueEffect},
    Maker{"SetCheckPoint", makeSetCheckPoint},
    Maker{"ShowProfileManager", makeShowProfileManager},
    Maker{"ShowRumbleModeInterface", makeShowRumbleModeInterface},
    Maker{"ToInt", makeToInt},
    Maker{"doFile", makeDoFile},
    Maker{"gc", makeGc},
    Maker{"isRelease", makeIsRelease},
    Maker{"preLoadFile", makePreLoadFile},
    Maker{"random", makeRandom},
};

// The string bindings: real, registered by addStringBindings() (scripting/config_strings.h).
constexpr std::array<std::string_view, 5> kStringBindings{"CfgAnnounceMessage", "CfgCrimeMessage", "CfgHUDMessage",
                                                          "CfgTutorialMessage", "CfgWarriorCommand"};

// Shorthands for the table below.
constexpr BindingInfo real(std::string_view name) {
    return BindingInfo{name, BindingKind::Real, StubResult::Nothing, false};
}
constexpr BindingInfo routed(std::string_view name) {
    return BindingInfo{name, BindingKind::Routed, StubResult::Nothing, false};
}
constexpr BindingInfo stub(std::string_view name, StubResult result = StubResult::Nothing) {
    return BindingInfo{name, BindingKind::Stub, result, false};
}
constexpr BindingInfo recording(std::string_view name) {
    return BindingInfo{name, BindingKind::Stub, StubResult::Nothing, true};
}

// The binding table, by name. Real and routed bindings have a maker above (or are string bindings); a stub returns its
// StubResult, and a recording stub also keeps its arguments. `coney-tools natives coney` reads this table (one
// `kind("Name"...)` entry per line) to set the Coney status in research/bindings/; run it after changing the table.
// std::to_array, not `std::array kBindings{`: deducing the type of hundreds of entries needs a fold expression deeper
// than Clang's nesting limit.
constexpr auto kBindings = std::to_array<BindingInfo>({
    // The script system and the game state.
    real("doFile"),
    real("preLoadFile"),
    real("ScheduleFunc"),
    real("ScheduleFuncArg1"),
    real("FlushScheduledFuncs"),
    real("gc"),
    real("GetGameTime"),
    real("random"),
    real("GetPlatform"),
    real("isRelease"),
    real("GetLanguage"),
    real("GetCurrentLevelIndex"),
    real("GetLevelId"),
    real("GetDifficulty"),
    real("GetProfileDifficulty"),
    real("GetCheckPoint"),
    real("SetCheckPoint"),
    real("ToInt"),
    // Configuration with a typed home.
    real("CfgLevelName"),
    real("CfgHUDMessage"),
    real("CfgCrimeMessage"),
    real("CfgTutorialMessage"),
    real("CfgWarriorCommand"),
    real("CfgAnnounceMessage"),
    // The front end.
    real("ShowProfileManager"),
    real("MenuLoadLevel"),
    real("ScreenQueueEffect"),
    real("HUDLaunchMissionComplete"),
    real("HUDLaunchMissionFailed"),
    // A Rumble match: the intro, the result screen, the tactics and the fighters (rumble_match_bindings.h).
    real("ShowRumbleModeIntro"),
    real("HUDLaunchRumbleWin"),
    real("TacticAttack"),
    real("TacticConfront"),
    real("BrFlushActions"),
    real("BrFlushGoals"),
    real("HuSetMaxHealth"),
    real("HuDelete"),
    real("HuGetGang"),
    real("HuSwitchPlayer"),
    real("CameraCreateWin"),
    real("CamSetFollowHeading"),
    real("CamDelete"),
    real("SSMC_StartDeleteSequence"),
    real("SSMC_StartLoadSequence"),
    // The level scripts' humans, flags, saved numbers, start callback and Rumble set-up (level_bindings.h).
    real("HuCreate"),
    real("AddFlag"),
    real("FindFlag"),
    real("GetFlagPos"),
    real("GetPosition"),
    real("HuGetPosition"),
    real("TeleportToFlag"),
    real("CfgSetDatabaseSizes"),
    real("GetLUASaveDataFloat"),
    real("SetLUASaveDataFloat"),
    real("SetStartGameCallback"),
    real("GetRumbleModeData"),
    real("GetRumbleModeGangName"),
    // The players' inventory, statistics, unlockables, stopwatch, crime reporting and pad handlers
    // (player_bindings.h).
    real("CfgHuInventoryCallback"),
    real("CfgInventoryCallback"),
    real("CfgInventoryItem"),
    real("CfgMoneyCallback"),
    real("CfgMultiplayerJoin"),
    real("SetMultiplayerCallback"),
    real("CfgSetStatTypeMax"),
    real("CfgSetStatValue"),
    real("CfgSetSteroTheftHandler"),
    real("EnterStore"),
    real("ExitStore"),
    real("GiveMoney"),
    real("InvGetMoney"),
    real("InvGetSpraycanCharges"),
    real("InvGiveItem"),
    real("InvGiveRevive"),
    real("InvGiveSkeletonKey"),
    real("InvNumberOf"),
    real("InvNumberRevives"),
    real("InvNumberSkeletonKeys"),
    real("InvPlayerHasItem"),
    real("InvSetMoney"),
    real("InvSetSpraycanCharges"),
    real("PadSetHandler"),
    real("PadSetHandlerEx"),
    real("ReportCrime"),
    real("StatAdd"),
    real("StatGetScore"),
    real("StatReset"),
    real("StatResetPlayer"),
    real("TakeMoney"),
    real("UM_GetRecordData"),
    real("UM_IsDataDirty"),
    real("UM_IsDataUnlocked"),
    real("UM_IsLevelComplete"),
    real("UM_IsTypeDirty"),
    real("UM_Reset"),
    real("UM_SetNumUnlockables"),
    real("UM_SetUnlockable"),
    real("UM_Unlock"),
    real("W_GetStopWatchTime"),
    real("W_SetStopWatch"),
    real("W_StartStopWatch"),
    real("W_ShowStopWatch"),
    // The objects' message handlers and the volume boxes that send them (trigger_bindings.h).
    real("AddVolumeBox"),
    real("RotateVolumeBox"),
    real("SetMsgHandler"),
    real("SetMsgHandlerEx"),
    // The animation callbacks (anim_callbacks.h).
    real("AddAnimCallback"),
    real("AddAllAnimCallback"),
    real("DelAnimCallback"),
    // The level scripts' goals and actions for a human's brain (ai_bindings.h).
    real("GoalMoveToFlag"),
    real("ActLookAt"),
    real("GoalFight"),
    real("BrFlush"),
    real("BrDead"),
    real("BrSuspend"),
    real("BrSetThreatResponse"),
    real("GoalPlayDynAnimation"),
    real("GoalAddressPerson"),
    real("GoalTrackHuman"),
    real("GoalDealer"),
    real("BrSetNumFollowSlots"),
    real("BrSetFollowSlot"),
    real("BrSetFollowSlotSet"),
    real("TacticCrowd"),
    real("TacticTrigger"),
    real("TacticClear"),
    // The cameras level99 drives (camera_bindings.h).
    real("CamSetupFollow"),
    real("CfgFollowCamera"),
    real("CameraCreateLocked"),
    real("CameraMakeActive"),
    real("CamLockLocked"),
    real("CameraReset"),
    real("CamSetFollowZoom"),
    real("CamAssignRevCamButton"),
    real("CamSetSplitMode"),
    real("CamSetFollowAngle"),
    real("CamSetSecondary"),
    real("CamEnable"),
    real("CamTarget"),
    // The gangs (gang_bindings.h, and GangCreate above).
    // The characters', brains' and gangs' bindings of the first mission (scripting/human_bindings.h).
    real("BrClearBackoff"),
    real("BrSetThugWantsWeapon"),
    real("CfgPlayerMugging"),
    real("CfgRageHandlers"),
    real("CfgSetDefaultFollowSlotSet"),
    real("CfgSetEnemySpotting"),
    real("CfgSetGlobalTimeToLive"),
    real("CfgSetWarriorSpotting"),
    real("EnableCommand"),
    real("EnableCommands"),
    real("GangAddSpawner"),
    real("GangClearResponders"),
    real("GangClearWanted"),
    real("GangInvincible"),
    real("GangSetTargetable"),
    real("GoalBackoff"),
    real("GoalBumLogic"),
    real("GoalMoveToUseFlag"),
    real("HuAttachSpinningIcon"),
    real("HuChangePlayerGang"),
    real("HuDropWeapon"),
    real("HuGetGangType"),
    real("HuGetHealthPercent"),
    real("HuGetHeldObject"),
    real("HuIsAPlayer"),
    real("HuIsAlive"),
    real("HuIsArrested"),
    real("HuLockPad"),
    real("HuPlaceItemInHand"),
    real("HuRemoveSpinningIcon"),
    real("HuRevive"),
    real("HuSetArrested"),
    real("HuSetCarriedItem"),
    real("HuSetDemiGodMode"),
    real("HuSetFastClimber"),
    real("HuSetFullRage"),
    real("HuSetGodMode"),
    real("HuSetHealthPercent"),
    real("HuSetIncreasedReact"),
    real("HuSetKeepWeapon"),
    real("HuSetLockedRage"),
    real("HuSetLookTarget"),
    real("HuSetMoney"),
    real("HuSetMugCallback"),
    real("HuSetNoTarget"),
    real("HuSetNoThrowWeapon"),
    real("HuSetNormalMode"),
    real("HuSetPreventRage"),
    real("HuSetPushable"),
    real("HuSetRageFrac"),
    real("HuSetReducedReact"),
    real("HuSetTireless"),
    real("HuSetUngrabbable"),
    real("HuSetUngroundable"),
    real("HuSetUnstunnable"),
    real("HuTeleportNearHuman"),
    real("HuUseAnim"),
    real("SetDynamicAnimation"),
    real("LoadBumAnims"),
    real("SetInterrogateParam"),
    real("WCEnableAllCommands"),
    real("WCIssueCommand"),
    // The story missions' cameras, particles, fog, litter and sound (story_effects_bindings.h).
    real("CamAddPoizoPoint"),
    real("CamAddPoizoPointCam"),
    real("CamSetupPoizo"),
    real("CameraGetActive"),
    real("CameraSetClipping"),
    real("CamGetPos"),
    real("CamSetFollowPos"),
    real("CfgSteam"),
    real("CfgTagSettings"),
    real("ProcessTag"),
    real("EndGarbage"),
    real("EndParticle"),
    real("MaxFogParticles"),
    real("SetupRadio"),
    real("SoundEnableSystemMusic"),
    real("SoundSetMusicTrack"),
    real("Start3DFog"),
    real("StartGarbage"),
    real("StartParticle"),
    // The hub, level95 (hub_bindings.h, hub_world_bindings.h), and its speech line and unlockables list
    // (sound_bindings.h, player_bindings.h).
    real("BrCanUseWorldFlags"),
    real("BrHasEnemies"),
    real("CarDestroy"),
    real("CfgActionDistance"),
    real("CfgEnableCrimeType"),
    real("CfgEnableTurfInvasion"),
    real("CfgObjectValueMod"),
    real("CfgPlayerCombatWalkOnly"),
    real("CfgStickDeflection"),
    real("CfgWorkoutParams"),
    real("CheckMultiplayer"),
    real("EnableAmbientEmitter"),
    real("FlagNetClear"),
    real("GangCanFlee"),
    real("GangClearBums"),
    real("GangClearHandlers"),
    real("GangGoodToGo"),
    real("GangIsASpawner"),
    real("GangMakeNeutralOfType"),
    real("GetLUASaveDataBool"),
    real("GetObjectName"),
    real("GetPTank"),
    real("GoalAreaWalker"),
    real("GoalBoxer"),
    real("GoalGrabTarget"),
    real("GoalPeddler"),
    real("GoalPlayGenAnim"),
    real("GoalShopkeeper"),
    real("HUDEnableClubActionText"),
    real("HUDShowMissionSelect"),
    real("HUDTurnOffActionCycleAnim"),
    real("HUDTurnOnActionCycleAnim"),
    real("HuAttachGear"),
    real("HuBlockTackle"),
    real("HuGetMoney"),
    real("HuGetVoiceIndex"),
    real("HuGiveCuffs"),
    real("HuIsDead"),
    real("HuPlayDynAnim"),
    real("HuSay"),
    real("HuSetCombatMode"),
    real("HuSetLookAtTarget"),
    real("HuSetMug"),
    real("HuSetName"),
    real("HuSetPedReaction"),
    real("HuSetScale"),
    real("HuSetUnarrestable"),
    real("HuSetWorkoutBlend"),
    real("HuSetWorkoutCallbacks"),
    real("HuStopWorkout"),
    real("HuWorkout"),
    real("KillParticle"),
    real("ObjIsAlive"),
    real("ObjMarkZone"),
    real("ReleasePTank"),
    real("ResetStore"),
    real("SSMC_StartSaveSequence"),
    real("SetLUASaveDataBool"),
    real("SetMotionAlpha"),
    real("ShowGameStatsInterface"),
    real("SndLoadMatrix"),
    real("SoundPlay"),
    real("UM_GetUnlockablesByType"),
    // The story's second and third missions (story_bindings.h).
    real("TacticAvoidEnemies"),
    real("TacticDefend"),
    real("TacticHanginOut"),
    real("TacticHoldTheLine"),
    real("TacticIdle"),
    real("TacticManWeaponPile"),
    real("TacticMoveToFlag"),
    real("TacticPursue"),
    real("TacticScout"),
    real("TacticSteal"),
    real("TacticTravelPath"),
    real("TacticUseFlag"),
    real("TacticVandalize"),
    real("TacticWalkinTall"),
    real("TacticWander"),
    real("AddPath"),
    real("BrSetFOV"),
    real("BrSetInvestigateResponse"),
    real("BrSetReactToViolence"),
    real("CfgCrimeResponders"),
    real("CfgEnableGrappleCounters"),
    real("CfgGangSizeForCombatMusic"),
    real("CfgSetOutdoorMode"),
    real("clearDetailFlag"),
    real("CrimeIsHappening"),
    real("EnableVolumeBox"),
    real("FlagGetOwner"),
    real("GangAddTurfBox"),
    real("GangCanUseWorldFlags"),
    real("GangEnableAttackStrategies"),
    real("GangEngageEnemy"),
    real("GangExitWorld"),
    real("GangGetLeader"),
    real("GangIsWanted"),
    real("GangRemoveTurfBox"),
    real("GangSetHearRange"),
    real("GangSetInvestigateResponse"),
    real("GangSetLeader"),
    real("GangSetRespondPercentage"),
    real("GangStartSpawner"),
    real("GetDistanceTweenHumans"),
    real("GoalBumLogicTrigger"),
    real("GoalMelee"),
    real("GoalMoveToExitFlag"),
    real("GoalPlayDynIdle"),
    real("GoalThrowObject"),
    real("GoalTravelPath"),
    real("HuAreActionsBlocked"),
    real("HuBlockLook"),
    real("HUDShowWarCommand"),
    real("HuExitWorld"),
    real("HuForceLook"),
    real("HuGetControlName"),
    real("HuIsAimingAt"),
    real("HuIsGrabbed"),
    real("HuKill"),
    real("HuLockPadMovement"),
    real("HuSetAutoEscape"),
    real("HuSetHealth"),
    real("HuSetKeepHat"),
    real("HuSetLOSRange"),
    real("HuSetRevivable"),
    real("HuShadow"),
    real("HuTag"),
    real("HuTagColor"),
    real("HuTagPattern"),
    real("HuWhatAmIHolding"),
    real("IsInsideBox"),
    real("IssueWarriorCommand"),
    real("PathValid"),
    real("SetCharacterModel"),
    real("setDetailFlag"),
    real("SetFlagPos"),
    real("SetSpawnMax"),
    real("TestDistance"),
    real("WalkingDistance"),
    real("WCEnableCommand"),
    real("WCLockCommands"),
    real("WCSetCallback"),
    real("GangCreate"),
    real("GangDelete"),
    real("GangAddMember"),
    real("GangBrDead"),
    real("GangBrFlush"),
    real("GangSetThreatResponse"),
    real("GangMakeEnemies"),
    real("GangMakeFriends"),
    real("GangSetMsgHandler"),
    real("GangSuspend"),
    real("GangGetHeadCount"),
    real("GangGetStandingCount"),
    // The breakable glass and the doors (object_bindings.h).
    real("SpawnBreakableGlass"),
    real("SpawnDoor"),
    real("CfgSetGlassProperties"),
    real("OpenDoor"),
    real("CloseDoor"),
    real("DisableDoorCollision"),
    real("ObjectChangeState"),
    real("SetDoorPickable"),
    real("DoorOpen"),
    real("OpenDoorAnimated"),
    real("DoorOpenDegree"),
    real("IsDoorOpen"),
    real("GetHitpoints"),
    real("GetLeftDoorHandle"),
    real("GetRightDoorHandle"),
    real("BreakGlassInRadius"),
    real("BreakObjectsInRadius"),
    real("DisableDoorLink"),
    real("EnableDoorLink"),
    real("ConvertJumpToDoor"),
    real("CfgSetLockPickHandler"),
    real("CfgSetLockPickStageFailHandler"),
    // The in-game HUD: the player panels, objectives, hints, counter panels, the arrow and the radars (hud_bindings.h).
    real("FlashRageBar"),
    real("ForceShowPlayerHud"),
    real("HUDAddRadarHuman"),
    real("HUDAddRadarMissionObjective"),
    real("HUDAddSecondaryRadarMissionObjective"),
    real("HUDCheckTutorialText"),
    real("HUDDeleteRadarMissionObjective"),
    real("HUDDeleteRadarObject"),
    real("HUDEnableGameTutorialText"),
    real("HUDEnableInstArrow"),
    real("HUDFlushTutorialText"),
    real("HUDGetNewPH"),
    real("HUDReleasePH"),
    real("HUDRemoveAllGoalText"),
    real("HUDSetAnnounceMsg"),
    real("HUDSetInstArrowAnimSpeed"),
    real("HUDSetNumIndicator"),
    real("HUDEnableTextProgress"),
    real("HUDSetTextProgress"),
    real("HUDSetObjective"),
    real("HUDSetPHValue"),
    real("HUDSetRadarItemTexture"),
    real("HUDSetRadarObjectFlash"),
    real("HUDSetTutorialCallback"),
    real("HUDSetTutorialText"),
    real("HUDShowMissionSummaryText"),
    real("HUDTurnOffRadar"),
    real("HUDTurnOnRadar"),
    real("HidePlayerHud"),
    real("HideHud"),
    real("RestoreHud"),
    real("ShowHud"),
    real("ShowPlayerHud"),
    // The Rumble menu's lists, which its chunks build (rumble_bindings.h).
    real("CfgRumbleGame"),
    real("CfgRumbleGang"),
    real("CfgRumbleArena"),
    real("CfgRumbleChar"),
    real("ScenePreload"),
    real("SceneIsPreloaded"),
    real("SceneUnload"),
    real("SceneSetCallback"),
    real("ScenePlayCinematic"),
    real("ScenePlayFixedScene"),
    real("ScenePlay"),
    real("ScenePlayAnimation"),
    real("SceneStop"),
    real("SceneTerminate"),
    real("SceneDone"),
    real("SceneLength"),
    real("SceneAddObject"),
    real("GoalJoinCinematic"),
    real("GoalJoinFixedScene"),
    real("GoalJoinAnimation"),
    // The particle systems and the motion blur (effects_bindings.h).
    real("SpawnParticle"),
    real("QueueMotionBlurEffect"),
    // The sound: configuration, ambience, music, the listener and speech (sound_bindings.h).
    real("SndCfgMusicInfo"),
    real("SoundCfgInterfaceSound"),
    real("SndAllocateCharacterVoices"),
    real("SndSetCommandSoundPercent"),
    real("SndLoadBank"),
    real("SndSetNIDuck"),
    real("SndSetPitchMod"),
    real("AddAmbientSound"),
    real("AddAmbientSoundEmitter2"),
    real("SetAmbientEmitterPositions"),
    real("SoundPlayAmbientTrack"),
    real("SoundPlay2D"),
    real("SoundPauseSound"),
    real("SoundStopAmbientTrack"),
    real("SetAmbientTrackVolume"),
    real("SoundPlayMusicTrack"),
    real("SoundLoopMusicTrack"),
    real("SoundStopMusicTrack"),
    real("SoundSetMusicVolume"),
    real("SndSetListener"),
    real("HuSpeak"),
    real("HuSpeakNI"),
    real("HuShutUp"),
    real("SoundPlayCommand"),
    // The parked cars (car_bindings.h).
    real("CarSpawn"),
    real("CarSetColor"),
    real("CarMakeGoodAsNew"),
    real("CarSpawnRadio"),
    real("CarPlaceInTrunkOnDetach"),
    // The dynamic objects' show, hide, destroy and zones, the trigger spheres, the flag network, the subtitle switch
    // and two empty ones (world_bindings.h).
    real("CfgSubtitles"),
    real("DoorCRCCheck"),
    real("EnableShadow"),
    real("FlagNetAddLink"),
    real("FlagNetTraverse"),
    real("ObjDestroy"),
    real("ObjEnableZone"),
    real("ObjHide"),
    real("ObjShow"),
    real("TriggerSphereCfg"),
    real("TriggerSphereEnable"),
    // The Rumble arenas' game mode, precache queue and switches, and the humans' movement lock, speech switch,
    // pocket, damage response and teleport (arena_bindings.h).
    real("ActGiveWay"),
    real("BrSetAttackWeight"),
    real("BrSetDamageResponse"),
    real("BrSetType"),
    real("CNSEnableMissionInfo"),
    real("GangAttachSpinningIcon"),
    real("GangRemoveSpinningIcon"),
    real("GangSetDamageResponse"),
    real("GetGameMode"),
    real("GoalEngageEnemy"),
    real("GoalMoveToHuman"),
    real("HuEnableSoundCommands"),
    real("HuForceEnableReticule"),
    real("HuGetCharType"),
    real("HuLockMovement"),
    real("HuPutItemInPocket"),
    real("HuSetConscious"),
    real("HuSetNoAutoLock"),
    real("HuSetSlowMo"),
    real("HuSetWheelchairControl"),
    real("ObjColor"),
    real("HuRemoveItemInPocket"),
    real("PrecacheWorld"),
    real("QueueFileToPrecache"),
    real("SetGameMode"),
    real("TacticDomination"),
    real("Teleport"),
    real("TurnWarriorCommands"),
    real("WCEnableAutomaticSwitching"),
    routed("ShowRumbleModeInterface"),
    routed("PlayMovie"),
    // The four the script system's constructor registers itself: configuration kept for later.
    recording("CfgChar"),
    recording("CfgObjectGroup"),
    recording("CfgGang"),
    recording("CfgSpeedClass"),

    // Stubs: the rest of what the front-end path calls (enum_preload.lua, the three config_preload*.lua,
    // config_strings_<lang>.lua, global.lua, level100.lua and the Menu callbacks). Configuration for subsystems Coney
    // does not have yet records its arguments; the rest does nothing.
    //
    // Characters, combat and the world's rules (config_preload2.lua).
    recording("CfgAnimSpeeds"),
    recording("CfgAttackDelay"),
    recording("CfgAttackFromIdle"),
    recording("CfgAutoLockAndCombat"),
    recording("CfgBaseChanceToBlock"),
    recording("CfgBreakAndEnterDelay"),
    recording("CfgBreathingSound"),
    recording("CfgBurnRates"),
    recording("CfgBurnTime"),
    recording("CfgButtonHeldFrames"),
    recording("CfgButtonMash"),
    recording("CfgCanBeAttackedModifier"),
    recording("CfgCharClassAttribs"),
    real("CfgCivilianAggression"),
    real("CfgDisableMusicForScenes"),
    recording("CfgDistances"),
    recording("CfgGangMusic"),
    recording("CfgGearData"),
    recording("CfgHat"),
    recording("CfgHUDColor"),
    recording("CfgJumpIsAction"),
    recording("CfgPickupIsAction"),
    recording("CfgPickupIsGrab"),
    recording("CfgPlayerRunButton"),
    recording("CfgPowerClass"),
    recording("CfgPowerEndurance"),
    recording("CfgRagePoints"),
    recording("CfgRagePowerMode"),
    recording("CfgScrFx"),
    recording("CfgSearchCounts"),
    recording("CfgSearchTimes"),
    recording("CfgSetMeleeRange"),
    recording("CfgSetTargetingPoints"),
    recording("CfgSetTargetingPointsEx"),
    recording("CfgSetTurnRates"),
    recording("CfgSnap"),
    real("CfgTagStartCallback"),
    recording("CfgTurnRate"),
    real("CfgVerticalSightModifier"),
    recording("CfgWarriorClass"),
    recording("CfgWarriorUpgrade"),
    // World objects (config_preload3.lua: 1,279 calls).
    real("CfgObj"),
    // Sound configuration (config_preload.lua, config_preload2.lua, global.lua).
    recording("DuplicateSoundMaterials"),
    recording("NewAnimSlots"),
    recording("NewAnimSound"),
    recording("NewMaterialSlots"),
    recording("NewMaterialSound"),
    recording("SetNumberOfMaterialSlots"),
    // Unlockables and commands (global.lua).
    recording("AddCommand"),
    // Sound and music state.
    real("SoundEnableEffects"),
    real("SoundSetEffect"),
    // Unlockables and saves: Coney has none.
    stub("ResetCommands"),
    // Game rules and callbacks.
    stub("SetCheatCallback"),
    stub("SetCopGuardArrestedRange"),
    stub("SetDeathTimer"),
    stub("SetDifficulty"),
    stub("SetGlobalPedRules"),
    // The level's lights and fog (scripting/lighting_bindings.h).
    real("SetFogColor"),
    real("SetFogDistance"),
    real("SetGammaOffset"),
    real("SetLight"),
    real("SetLightFlicker"),
    real("SetWorldAmbient"),
    // Lighting, weather and screen effects of the level.
    stub("End3DFog"),
    stub("EndFog"),
    stub("EndRain"),
    stub("EndRoomSmoke"),
    stub("SetLevelColour"),
    stub("SetShadowColor"),
    stub("SetShadowLightOffset"),
    // The HUD.
    // Scenes, objects and particles: the ones that make something return a handle.
    real("ObjSpawn"),
});

// A stub's function: keeps the arguments when it records, then returns its default.
NativeFunction makeStub(const BindingInfo& info, const BindingContext& context,
                        const std::shared_ptr<HandleCounter>& handles) {
    return [info, recorded = context.recorded, handles](std::span<const Value> args) -> binding::Results {
        if (info.records && recorded != nullptr) {
            recorded->add(info.name, args);
        }
        switch (info.stubResult) {
        case StubResult::Nothing:
            return binding::none();
        case StubResult::Handle: {
            const double handle = handles->next;
            handles->next += 1;
            return binding::number(handle);
        }
        case StubResult::Zero:
            return binding::number(0.0);
        case StubResult::False:
            return binding::boolean(false);
        case StubResult::True:
            return binding::boolean(true);
        }
        return binding::none();
    };
}

} // namespace

std::span<const BindingInfo> bindingTable() { return kBindings; }

std::size_t bindingCount(BindingKind kind) {
    return static_cast<std::size_t>(
        std::ranges::count_if(kBindings, [kind](const BindingInfo& info) { return info.kind == kind; }));
}

void RecordedCalls::add(std::string_view binding, std::span<const Value> args) {
    std::vector<Value> kept;
    kept.reserve(args.size());
    for (const Value& arg : args) {
        // Numbers and strings as they are; a table as a fresh list of its numbers at 1, 2, ... (a table of the
        // state could hold it alive, and only such lists are configuration data: `CfgChar`'s damage and attack
        // tables); anything else as nil.
        if (arg.type() == Value::Type::Number || arg.type() == Value::Type::String) {
            kept.push_back(arg);
        } else if (const std::shared_ptr<Table>& table = arg.table(); table != nullptr) {
            auto list = std::make_shared<Table>();
            for (double key = 1.0;; key += 1.0) {
                const std::optional<double> number = table->get(Value(key)).number();
                if (!number.has_value()) {
                    break;
                }
                // A number key and a number value cannot fail.
                const bool stored = list->set(Value(key), Value(*number)).has_value();
                CONEY_ASSERT(stored);
            }
            kept.emplace_back(std::move(list));
        } else {
            kept.emplace_back();
        }
    }
    auto found = m_calls.find(binding);
    if (found == m_calls.end()) {
        found = m_calls.emplace(std::string(binding), std::vector<std::vector<Value>>{}).first;
    }
    found->second.push_back(std::move(kept));
}

std::span<const std::vector<Value>> RecordedCalls::calls(std::string_view binding) const {
    const auto found = m_calls.find(binding);
    return found == m_calls.end() ? std::span<const std::vector<Value>>{} : std::span(found->second);
}

std::size_t RecordedCalls::total() const {
    std::size_t total = 0;
    for (const auto& entry : m_calls) {
        total += entry.second.size();
    }
    return total;
}

void installBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context) {
    CONEY_ASSERT(context.state != nullptr && context.strings != nullptr && context.host != nullptr);
    const Factory factory{&scripts, &context, std::make_shared<HandleCounter>()};
    // A fresh state spawns its level's objects anew; the last state's handles name nothing in it. The object types
    // stay: the legal screen's preloads configure them once (`config_preload3.lua`), and no later state runs them
    // again (scripting.md, "Life of the Lua state").
    if (context.spawnRecords != nullptr) {
        context.spawnRecords->clear();
    }
    for (const BindingInfo& info : kBindings) {
        if (info.kind == BindingKind::Stub) {
            vm.registerFunction(info.name, makeStub(info, context, factory.handles));
            continue;
        }
        const auto maker = std::ranges::find(kMakers, info.name, &Maker::name);
        if (maker != kMakers.end()) {
            vm.registerFunction(info.name, maker->make(factory));
            continue;
        }
        // Every real binding has a maker or is a string, level, Rumble, AI, gang, scene, lighting or HUD
        // binding (CONEY_ASSERT).
        CONEY_ASSERT(std::ranges::find(kStringBindings, info.name) != kStringBindings.end() ||
                     std::ranges::find(kLevelBindings, info.name) != kLevelBindings.end() ||
                     std::ranges::find(kRumbleBindings, info.name) != kRumbleBindings.end() ||
                     std::ranges::find(kRumbleMatchBindings, info.name) != kRumbleMatchBindings.end() ||
                     std::ranges::find(kAiBindings, info.name) != kAiBindings.end() ||
                     std::ranges::find(kCameraBindings, info.name) != kCameraBindings.end() ||
                     std::ranges::find(kGangBindings, info.name) != kGangBindings.end() ||
                     std::ranges::find(kTriggerBindings, info.name) != kTriggerBindings.end() ||
                     std::ranges::find(kAnimCallbackBindings, info.name) != kAnimCallbackBindings.end() ||
                     std::ranges::find(kSceneBindings, info.name) != kSceneBindings.end() ||
                     std::ranges::find(kSoundBindings, info.name) != kSoundBindings.end() ||
                     std::ranges::find(kSpawnBindings, info.name) != kSpawnBindings.end() ||
                     std::ranges::find(kObjectBindings, info.name) != kObjectBindings.end() ||
                     std::ranges::find(kLightingBindings, info.name) != kLightingBindings.end() ||
                     std::ranges::find(kPlayerBindings, info.name) != kPlayerBindings.end() ||
                     std::ranges::find(kHumanBindings, info.name) != kHumanBindings.end() ||
                     std::ranges::find(kEffectsBindings, info.name) != kEffectsBindings.end() ||
                     std::ranges::find(kCarBindings, info.name) != kCarBindings.end() ||
                     std::ranges::find(kHudBindings, info.name) != kHudBindings.end() ||
                     std::ranges::find(kWorldBindings, info.name) != kWorldBindings.end() ||
                     std::ranges::find(kStoryBindings, info.name) != kStoryBindings.end() ||
                     std::ranges::find(kArenaBindings, info.name) != kArenaBindings.end() ||
                     std::ranges::find(kStoryEffectsBindings, info.name) != kStoryEffectsBindings.end() ||
                     std::ranges::find(kHubBindings, info.name) != kHubBindings.end() ||
                     std::ranges::find(kHubWorldBindings, info.name) != kHubWorldBindings.end());
    }
    addStringBindings(vm, *context.strings);
    addRumbleBindings(vm, context);
    addAiBindings(vm, context);
    addGangBindings(vm, context);
    addAnimCallbackBindings(vm, context);
    addLightingBindings(vm, context);
    addPlayerBindings(scripts, vm, context);
    addSoundBindings(scripts, vm, context);
    addHudBindings(vm, context);
    // With no scene system at the call (a test, the menus, a mode that plays no scenes), the stand-in keeps the
    // scripts' scene flow moving.
    addSceneBindings(vm, context,
                     SceneStandIn{.preload = makeStandInScenePreload(factory), .play = makeStandInScenePlay(factory)});
    // The level, trigger, camera and object bindings make world objects, so they take their handles from the same
    // counter as the stubs.
    const std::function<double()> nextHandle = [handles = factory.handles] {
        const double handle = handles->next;
        handles->next += 1;
        return handle;
    };
    addLevelBindings(vm, context, nextHandle);
    addTriggerBindings(vm, context, nextHandle);
    addCameraBindings(vm, context, nextHandle);
    addSpawnBindings(vm, context, nextHandle);
    addObjectBindings(vm, context, nextHandle);
    addRumbleMatchBindings(vm, context, nextHandle);
    addHumanBindings(vm, context, nextHandle);
    addStoryBindings(scripts, vm, context, nextHandle);
    addStoryEffectsBindings(vm, context, nextHandle);
    addEffectsBindings(vm, context, nextHandle);
    addCarBindings(vm, context, nextHandle);
    addArenaBindings(vm, context);
    addWorldBindings(vm, context);
    addHubBindings(scripts, vm, context);
    addHubWorldBindings(scripts, vm, context);

    // The tolua support the registration also makes: the table `tolua`, the classes `M_Vector4` and `M_Quat`, and the
    // variables `NilHandle` and `NilSoundHandle`. Coney's choices: the classes are empty tables (no usertypes yet) and
    // both nil handles are 0, below the first handle a stub gives out; the page does not give their values.
    vm.setGlobal("tolua", Value(std::make_shared<Table>()));
    vm.setGlobal("M_Vector4", Value(std::make_shared<Table>()));
    vm.setGlobal("M_Quat", Value(std::make_shared<Table>()));
    vm.setGlobal("NilHandle", Value(kNilHandle));
    vm.setGlobal("NilSoundHandle", Value(0.0));
}

} // namespace coney::script
