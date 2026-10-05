// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/script_bindings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <utility>

#include "core/assert.h"
#include "scripting/config_strings.h"

namespace coney::script {

namespace {

// What a binding returns: its results, or an error that stops the script.
using Results = std::expected<std::vector<Value>, Error>;

// The value of the global `NilHandle` (Coney's choice, below): what a binding returns for no object.
constexpr double kNilHandle = 0.0;

// ---- The tolua argument and result conventions (docs/research/scripting.md#argument-and-result-conventions) ----

// Argument `i` (0-based) as a number: 0 when absent or not convertible, as tolua reads a missing argument.
double numberArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size()) {
        return 0.0;
    }
    if (const std::optional<double> number = args[i].number()) {
        return *number;
    }
    if (const std::optional<std::string_view> text = args[i].string()) {
        return parseLuaNumber(*text).value_or(0.0);
    }
    return 0.0;
}

// Argument `i` as a string: a string, a number's text, or empty (tolua's NULL) for anything else.
std::string stringArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size()) {
        return {};
    }
    if (const std::optional<std::string_view> text = args[i].string()) {
        return std::string(*text);
    }
    if (const std::optional<double> number = args[i].number()) {
        return std::format("{:.16g}", *number);
    }
    return {};
}

// No results.
Results none() { return std::vector<Value>{}; }
// One number.
Results number(double value) { return std::vector<Value>{Value(value)}; }
// A boolean as tolua pushes it: the number 1 for true, nil for false (Lua 4.0 has no booleans).
Results boolean(bool value) { return std::vector<Value>{value ? Value(1.0) : Value()}; }

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
    return [](std::span<const Value>) { return number(kPlatformValue); };
}

// `isRelease()`: always true.
// @orig 0x00357990 isRelease (unknown)
NativeFunction makeIsRelease(const Factory& /*factory*/) {
    return [](std::span<const Value>) { return boolean(true); };
}

// `GetLanguage()`: the game state's language, 0 English to 4 German.
// @orig 0x0041d7f0 GetLanguage (unknown)
NativeFunction makeGetLanguage(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value>) {
        return number(static_cast<double>(state->language));
    };
}

// `GetCurrentLevelIndex()`: the current level record's index; 0, the front end, at start-up.
// @orig 0x0041d718 GetCurrentLevelIndex (unknown)
NativeFunction makeGetCurrentLevelIndex(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value>) {
        return number(static_cast<double>(state->currentLevel));
    };
}

// `GetLevelId(i)`: record i's level number; 0 for an index with no record (Coney's choice: the original reads a
// cleared record).
// @orig 0x0041d6f0 GetLevelId (unknown)
NativeFunction makeGetLevelId(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value> args) {
        const double index = numberArg(args, 0);
        const LevelRecord* record = index >= 0.0 && index < static_cast<double>(LevelTable::kCapacity)
                                        ? state->levels.at(static_cast<std::size_t>(index))
                                        : nullptr;
        return number(record != nullptr ? record->number : 0.0);
    };
}

// `GetDifficulty()`.
// @orig 0x0041d800 GetDifficulty (unknown)
NativeFunction makeGetDifficulty(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value>) { return number(state->difficulty); };
}

// `GetProfileDifficulty()`.
// @orig 0x0041d820 GetProfileDifficulty (unknown)
NativeFunction makeGetProfileDifficulty(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value>) { return number(state->profileDifficulty); };
}

// `GetCheckPoint()`: the level's current section.
// @orig 0x0041abe8 GetCheckPoint (unknown)
NativeFunction makeGetCheckPoint(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value>) { return number(state->checkPoint); };
}

// `SetCheckPoint(n)`: sets the section GetCheckPoint reads (the setter's address is not on the page).
NativeFunction makeSetCheckPoint(const Factory& factory) {
    return [state = factory.context->state](std::span<const Value> args) {
        state->checkPoint = numberArg(args, 0);
        return none();
    };
}

// `UM_IsLevelComplete(n)`: whether the unlockables manager has level n done. Coney has no saves, so no manager: false,
// as the original answers without one.
// @orig 0x004238a8 UM_IsLevelComplete (unknown)
NativeFunction makeIsLevelComplete(const Factory& /*factory*/) {
    return [](std::span<const Value>) { return boolean(false); };
}

// `ToInt(x)`: x truncated towards zero.
// @orig 0x0036d938 ToInt (unknown)
NativeFunction makeToInt(const Factory& /*factory*/) {
    return [](std::span<const Value> args) { return number(std::trunc(numberArg(args, 0))); };
}

// `doFile(name)`: runs `name.lua` now, in this state.
// @orig 0x003579a0 doFile (unknown)
NativeFunction makeDoFile(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        scripts->runFile(std::format("{}.lua", stringArg(args, 0)));
        return none();
    };
}

// `preLoadFile(name, callback)`: the original asks for `name.lua` without waiting and runs it when it arrives; Coney's
// reads are synchronous, so it runs at once. What the original does with `callback` is not traced: Coney ignores it.
// @orig 0x00357a68 preLoadFile (unknown)
NativeFunction makePreLoadFile(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        scripts->runFile(std::format("{}.lua", stringArg(args, 0)));
        return none();
    };
}

// `ScheduleFunc(name, ms)`: calls `name` ms milliseconds of game time from now.
// @orig 0x003863d8 ScheduleFunc (unknown)
NativeFunction makeScheduleFunc(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        scripts->schedule(stringArg(args, 0), static_cast<std::uint64_t>(std::max(0.0, numberArg(args, 1))));
        return none();
    };
}

// `ScheduleFuncArg1(name, ms, n)`: the same with one number argument.
// @orig 0x00386410 ScheduleFuncArg1 (unknown)
NativeFunction makeScheduleFuncArg1(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        const std::array<double, 1> callArgs{numberArg(args, 2)};
        scripts->schedule(stringArg(args, 0), static_cast<std::uint64_t>(std::max(0.0, numberArg(args, 1))), callArgs);
        return none();
    };
}

// `FlushScheduledFuncs(name)`: drops the scheduled calls of `name`, or all of them without a name.
// @orig 0x00386450 FlushScheduledFuncs (unknown)
NativeFunction makeFlushScheduledFuncs(const Factory& factory) {
    return [scripts = factory.scripts](std::span<const Value> args) {
        scripts->flushScheduled(stringArg(args, 0));
        return none();
    };
}

// `gc()`: collect garbage now; Coney's VM has none to collect.
// @orig 0x00386370 gc (unknown)
NativeFunction makeGc(const Factory& /*factory*/) {
    return [](std::span<const Value>) { return none(); };
}

// `ShowProfileManager(onRumble, onStartGame)`: shows the menus with the two callbacks.
// @orig 0x0036eef8 ShowProfileManager_Binding (unknown)
NativeFunction makeShowProfileManager(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->showProfileManager(stringArg(args, 0), stringArg(args, 1));
        return none();
    };
}

// `MenuLoadLevel(name)`: chooses the level to start.
// @orig 0x0036df48 MenuLoadLevel (unknown)
NativeFunction makeMenuLoadLevel(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->menuLoadLevel(stringArg(args, 0));
        return none();
    };
}

// `ScreenQueueEffect(type, seconds)`: queues a fade (0 in, 1 out).
NativeFunction makeScreenQueueEffect(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->queueScreenEffect(static_cast<int>(numberArg(args, 0)), numberArg(args, 1));
        return none();
    };
}

// `CfgLevelName(...)`: one level record from its 18 arguments (LevelRecord gives Coney's reading of their order).
// @orig 0x0036b220 CfgLevelName (unknown)
NativeFunction makeCfgLevelName(const Factory& factory) {
    return [state = factory.context->state, scripts = factory.scripts](std::span<const Value> args) {
        LevelRecord record;
        record.id = numberArg(args, 0);
        record.name = stringArg(args, 1);
        record.secondName = stringArg(args, 2);
        record.worldName = stringArg(args, 3);
        record.fourthName = stringArg(args, 4);
        record.number = numberArg(args, 5);
        for (std::size_t i = 0; i < record.values.size(); ++i) {
            record.values.at(i) = numberArg(args, 6 + i);
        }
        if (!state->levels.set(std::move(record))) {
            scripts->log(
                std::format("CfgLevelName: record index {} is outside the level table; ignored", numberArg(args, 0)));
        }
        return none();
    };
}

// `random(a, b)`: the game's own generator replaces the math library's. Its algorithm is not on the page, so Coney
// draws whole numbers in [a, b] from its own deterministic generator (seeded once per state).
NativeFunction makeRandom(const Factory& /*factory*/) {
    auto state = std::make_shared<std::uint32_t>(0x12345678U);
    return [state](std::span<const Value> args) {
        // xorshift32: small, deterministic, never the C library's.
        std::uint32_t x = *state;
        x ^= x << 13U;
        x ^= x >> 17U;
        x ^= x << 5U;
        *state = x;
        const double low = numberArg(args, 0);
        const double high = numberArg(args, 1);
        if (high <= low) {
            return number(low);
        }
        const double span = std::floor(high - low) + 1.0;
        return number(low + std::floor(static_cast<double>(x) / 4294967296.0 * span));
    };
}

// The position argument of `HuCreate`: a table of three numbers at t[1]..t[3]; nothing for anything else.
std::optional<std::array<float, 3>> positionArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].table() == nullptr) {
        return std::nullopt;
    }
    const Table& table = *args[i].table();
    std::array<float, 3> position{};
    for (std::size_t axis = 0; axis < position.size(); ++axis) {
        const std::optional<double> value = table.get(Value(static_cast<double>(axis + 1))).number();
        if (!value) {
            return std::nullopt;
        }
        position.at(axis) = static_cast<float>(*value);
    }
    return position;
}

// `HuCreate(name, type, {x, y, z}, heading, unused, player, gang, flag)`: makes a human and returns its handle, or
// `NilHandle` when every slot is taken. Coney has no characters as game objects yet, so the human is kept in the
// context's CreatedHumans, where the play mode takes player 1 from. Coney's choices: the position is not snapped to
// the ground here (no collision is loaded while the script runs) and so not written back into the table; the play mode
// snaps the player the same way when it places him (docs/research/characters.md#creation). The gang, the unused string
// and the flag are not kept.
// @orig 0x00358428 HuCreate (unknown)
// @orig 0x00233d60 Human_Create (unknown)
NativeFunction makeHuCreate(const Factory& factory) {
    return [humans = factory.context->humans, handles = factory.handles](std::span<const Value> args) {
        HumanCreation human;
        human.name = stringArg(args, 0);
        human.type = static_cast<int>(std::trunc(numberArg(args, 1)));
        human.position = positionArg(args, 2);
        human.headingDegrees = static_cast<float>(numberArg(args, 3));
        human.playerIndex = static_cast<int>(std::trunc(numberArg(args, 5)));
        human.handle = handles->next;
        if (humans != nullptr && !humans->add(human)) {
            return number(kNilHandle);
        }
        handles->next += 1;
        return number(human.handle);
    };
}

// `HUDLaunchMissionComplete(kind)`: shows the mission-complete mode with `kind` (runNextMission passes 4).
// @orig 0x0036f218 HUDLaunchMissionComplete (unknown)
NativeFunction makeHudLaunchMissionComplete(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->launchMissionComplete(static_cast<int>(std::trunc(numberArg(args, 0))));
        return none();
    };
}

// ---- Routed bindings: handed to the host's stand-ins ----

// `PlayMovie(name)`.
NativeFunction makePlayMovie(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->playMovie(stringArg(args, 0));
        return none();
    };
}

// `SoundPlayMusicTrack(track)` and `SoundLoopMusicTrack(track)`.
NativeFunction makePlayMusic(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->playMusic(stringArg(args, 0));
        return none();
    };
}

// `SoundStopMusicTrack()`.
NativeFunction makeStopMusic(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value>) {
        host->stopMusic();
        return none();
    };
}

// `ShowRumbleModeInterface(onCancel, onStart, n)`.
NativeFunction makeShowRumbleModeInterface(const Factory& factory) {
    return [host = factory.context->host](std::span<const Value> args) {
        host->showRumbleModeInterface(stringArg(args, 0), stringArg(args, 1), numberArg(args, 2));
        return none();
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
    // The level scripts' humans.
    real("HuCreate"),
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
    recording("CfgSetDatabaseSizes"),
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
    // Unlockables and saves: Coney has none.
    stub("ResetCommands"),
    stub("SetLUASaveDataBool"),
    stub("SetLUASaveDataFloat"),
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
    return [info, recorded = context.recorded, handles](std::span<const Value> args) -> Results {
        if (info.records && recorded != nullptr) {
            recorded->add(info.name, args);
        }
        switch (info.stubResult) {
        case StubResult::Nothing:
            return none();
        case StubResult::Handle: {
            const double handle = handles->next;
            handles->next += 1;
            return number(handle);
        }
        case StubResult::Zero:
            return number(0.0);
        case StubResult::False:
            return boolean(false);
        case StubResult::True:
            return boolean(true);
        }
        return none();
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
        // Every real binding has a maker or is a string binding (CONEY_ASSERT).
        CONEY_ASSERT(std::ranges::find(kStringBindings, info.name) != kStringBindings.end());
    }
    addStringBindings(vm, *context.strings);

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
