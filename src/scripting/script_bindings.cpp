// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/script_bindings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <utility>

#include "characters/character_class.h"
#include "core/assert.h"
#include "scripting/binding_args.h"
#include "scripting/config_strings.h"
#include "scripting/level_bindings.h"
#include "scripting/rumble_bindings.h"

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

// `SetCheckPoint(n)`: sets the section GetCheckPoint reads (the setter's address is not on the page).
NativeFunction makeSetCheckPoint(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value> args) {
        state->checkPoint = binding::number(args, 0);
        return binding::none();
    };
}

// `UM_IsLevelComplete(n)`: whether the unlockables manager has level n done. Coney has no saves, so no manager: false,
// as the original answers without one.
// @orig 0x004238a8 UM_IsLevelComplete (unknown)
NativeFunction makeIsLevelComplete(const Factory& /*factory*/) {
    return [](std::span<const Value>) { return binding::boolean(false); };
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
    return [host = factory.context->host](std::span<const Value> args) {
        host->queueScreenEffect(static_cast<int>(binding::number(args, 0)), binding::number(args, 1));
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
// `NilHandle` when every slot is taken. Coney has no characters as game objects yet, so the human is kept in the
// context's CreatedHumans, where the play mode takes player 1 from. Coney's choices: the position is not snapped to
// the ground here (no collision is loaded while the script runs) and so not written back into the table; the play mode
// snaps the player the same way when it places him (docs/research/characters.md#creation). The gang, the unused string
// and the flag are not kept. The model the type is drawn as is resolved here from the recorded `CfgChar` calls
// (characters::modelNameFor(), docs/research/characters.md#type-to-model); the play mode loads it.
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
        return binding::number(human.handle);
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

// ---- Routed bindings: handed to the host's stand-ins ----

// `PlayMovie(name)`.
NativeFunction makePlayMovie(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->playMovie(binding::string(args, 0));
        return binding::none();
    };
}

// `SoundPlayMusicTrack(track)` and `SoundLoopMusicTrack(track)`.
NativeFunction makePlayMusic(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->playMusic(binding::string(args, 0));
        return binding::none();
    };
}

// `SoundStopMusicTrack()`.
NativeFunction makeStopMusic(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value>) {
        host->stopMusic();
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
    Maker{"GetCheckPoint", makeGetCheckPoint},
    Maker{"GetCurrentLevelIndex", makeGetCurrentLevelIndex},
    Maker{"GetDifficulty", makeGetDifficulty},
    Maker{"GetGameTime", makeGetGameTime},
    Maker{"GetLanguage", makeGetLanguage},
    Maker{"GetLevelId", makeGetLevelId},
    Maker{"GetPlatform", makeGetPlatform},
    Maker{"GetProfileDifficulty", makeGetProfileDifficulty},
    Maker{"HUDLaunchMissionComplete", makeHudLaunchMissionComplete},
    Maker{"HuCreate", makeHuCreate},
    Maker{"MenuLoadLevel", makeMenuLoadLevel},
    Maker{"PlayMovie", makePlayMovie},
    Maker{"ScheduleFunc", makeScheduleFunc},
    Maker{"ScheduleFuncArg1", makeScheduleFuncArg1},
    Maker{"ScreenQueueEffect", makeScreenQueueEffect},
    Maker{"SetCheckPoint", makeSetCheckPoint},
    Maker{"ShowProfileManager", makeShowProfileManager},
    Maker{"ShowRumbleModeInterface", makeShowRumbleModeInterface},
    Maker{"SoundLoopMusicTrack", makePlayMusic},
    Maker{"SoundPlayMusicTrack", makePlayMusic},
    Maker{"SoundStopMusicTrack", makeStopMusic},
    Maker{"ToInt", makeToInt},
    Maker{"UM_IsLevelComplete", makeIsLevelComplete},
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
constexpr std::array kBindings{
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
    real("UM_IsLevelComplete"),
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
    // The level scripts' humans, flags, saved numbers, start callback and Rumble set-up (level_bindings.h).
    real("HuCreate"),
    real("AddFlag"),
    real("FindFlag"),
    real("GetFlagPos"),
    real("GetPosition"),
    real("TeleportToFlag"),
    real("CfgSetDatabaseSizes"),
    real("GetLUASaveDataFloat"),
    real("SetLUASaveDataFloat"),
    real("SetStartGameCallback"),
    real("GetRumbleModeData"),
    real("GetRumbleModeGangName"),
    // The Rumble menu's lists, which its chunks build (rumble_bindings.h).
    real("CfgRumbleGame"),
    real("CfgRumbleGang"),
    real("CfgRumbleArena"),
    real("CfgRumbleChar"),
    routed("ShowRumbleModeInterface"),
    routed("PlayMovie"),
    routed("SoundPlayMusicTrack"),
    routed("SoundLoopMusicTrack"),
    routed("SoundStopMusicTrack"),
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
    recording("CfgActionDistance"),
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
    recording("CfgCivilianAggression"),
    recording("CfgDisableMusicForScenes"),
    recording("CfgDistances"),
    recording("CfgEnableTurfInvasion"),
    recording("CfgGangMusic"),
    recording("CfgGearData"),
    recording("CfgHat"),
    recording("CfgHUDColor"),
    recording("CfgInventoryItem"),
    recording("CfgJumpIsAction"),
    recording("CfgPickupIsAction"),
    recording("CfgPickupIsGrab"),
    recording("CfgPlayerCombatWalkOnly"),
    recording("CfgPlayerRunButton"),
    recording("CfgPowerClass"),
    recording("CfgPowerEndurance"),
    recording("CfgRageHandlers"),
    recording("CfgRagePoints"),
    recording("CfgRagePowerMode"),
    recording("CfgScrFx"),
    recording("CfgSearchCounts"),
    recording("CfgSearchTimes"),
    recording("CfgSetDefaultFollowSlotSet"),
    recording("CfgSetGlassProperties"),
    recording("CfgSetGlobalTimeToLive"),
    recording("CfgSetMeleeRange"),
    recording("CfgSetStatTypeMax"),
    recording("CfgSetStatValue"),
    recording("CfgSetTargetingPoints"),
    recording("CfgSetTargetingPointsEx"),
    recording("CfgSetTurnRates"),
    recording("CfgSnap"),
    recording("CfgStickDeflection"),
    recording("CfgTagStartCallback"),
    recording("CfgTurnRate"),
    recording("CfgVerticalSightModifier"),
    recording("CfgWarriorClass"),
    recording("CfgWarriorUpgrade"),
    recording("CfgWorkoutParams"),
    // World objects (config_preload3.lua: 1,279 calls).
    recording("CfgObj"),
    // Sound configuration (config_preload.lua, config_preload2.lua, global.lua).
    recording("AddAmbientSound"),
    recording("DuplicateSoundMaterials"),
    recording("NewAnimSlots"),
    recording("NewAnimSound"),
    recording("NewMaterialSlots"),
    recording("NewMaterialSound"),
    recording("SetNumberOfMaterialSlots"),
    recording("SndAllocateCharacterVoices"),
    recording("SndCfgMusicInfo"),
    recording("SndSetCommandSoundPercent"),
    recording("SoundCfgInterfaceSound"),
    // Unlockables and commands (global.lua).
    recording("AddCommand"),
    recording("UM_SetNumUnlockables"),
    recording("UM_SetUnlockable"),
    // Sound and music state.
    stub("SndLoadMatrix"),
    stub("SndSetListener"),
    stub("SoundEnableEffects"),
    stub("SoundSetEffect"),
    stub("SoundSetMusicVolume"),
    // The inventory: Coney has none yet, so a new game's empty one.
    stub("InvNumberOf", StubResult::Zero),
    // Unlockables and saves: Coney has none.
    stub("ResetCommands"),
    stub("SetLUASaveDataBool"),
    stub("UM_IsTypeDirty", StubResult::False),
    stub("UM_Reset"),
    stub("UM_Unlock"),
    // Game rules and callbacks.
    stub("SetCheatCallback"),
    stub("SetCopGuardArrestedRange"),
    stub("SetDeathTimer"),
    stub("SetDifficulty"),
    stub("SetGlobalPedRules"),
    // Lighting, weather and screen effects of the level.
    stub("End3DFog"),
    stub("EndFog"),
    stub("EndRain"),
    stub("EndRoomSmoke"),
    stub("SetGammaOffset"),
    stub("SetLevelColour"),
    stub("SetLight"),
    stub("SetMotionAlpha"),
    stub("SetShadowColor"),
    stub("SetShadowLightOffset"),
    stub("SetWorldAmbient"),
    // The HUD.
    stub("HUDEnableClubActionText"),
    // Cameras, scenes, objects and particles: the ones that make something return a handle.
    stub("CameraCreateLocked", StubResult::Handle),
    stub("CameraMakeActive"),
    stub("CameraReset"),
    stub("GangCreate", StubResult::Handle),
    stub("GangGetHeadCount", StubResult::Zero),
    stub("GetPTank", StubResult::Handle),
    stub("ObjSpawn", StubResult::Handle),
    stub("ReleasePTank"),
    stub("SceneIsPreloaded", StubResult::False),
    stub("ScenePreload", StubResult::Handle),
    stub("SceneStop"),
};

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
        // Keep plain values only: a table could hold the state alive and is not configuration data.
        const bool plain = arg.type() == Value::Type::Number || arg.type() == Value::Type::String;
        kept.push_back(plain ? arg : Value());
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
        // Every real binding has a maker or is a string, level or Rumble binding (CONEY_ASSERT).
        CONEY_ASSERT(std::ranges::find(kStringBindings, info.name) != kStringBindings.end() ||
                     std::ranges::find(kLevelBindings, info.name) != kLevelBindings.end() ||
                     std::ranges::find(kRumbleBindings, info.name) != kRumbleBindings.end());
    }
    addStringBindings(vm, *context.strings);
    addRumbleBindings(vm, context);
    // The level bindings make world objects, so they take their handles from the same counter as the stubs.
    addLevelBindings(vm, context, [handles = factory.handles] {
        const double handle = handles->next;
        handles->next += 1;
        return handle;
    });

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
