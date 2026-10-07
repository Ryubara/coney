// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/hub_world_bindings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <vector>

#include "core/name_hash.h"
#include "effects/level_effects.h"
#include "hud/hud.h"
#include "human/locomotion.h"
#include "scripting/binding_args.h"
#include "scripting/sound_bindings.h"
#include "warriors/game_state.h"
#include "world_objects/cars.h"
#include "world_objects/flag_net.h"
#include "world_objects/flags.h"
#include "world_objects/spawn_records.h"

namespace coney::script {

namespace {

// The crime types `CfgEnableCrimeType` takes (0-14).
constexpr int kCrimeTypes = 15;
// The saved script flags: 1-128.
constexpr int kSavedFlags = 128;
// A store front flag's activity (`kind`), and its group word's bits (`+0xd8`): the robbed bit, the byte that
// `ResetStore` sets to 0xff and the gang field it sets to 31 (no gang).
constexpr int kStoreActivity = 14;
constexpr std::uint32_t kRobbedBit = 1U << 16;
constexpr std::uint32_t kStoreByte = 0xffU << 8;
constexpr std::uint32_t kStoreGangMask = 0x1fU << 18;
// `SndLoadMatrix`'s preload script: the matrix's name and this.
constexpr std::string_view kMatrixPreload = "{}_preload.lua";
// A sprite batch's handle is its slot shifted left this far.
constexpr int kPTankShift = 16;
// The prompts' players.
constexpr std::size_t kPromptPlayers = 2;

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as an integer.
int intArg(std::span<const Value> args, std::size_t i) { return static_cast<int>(wholeArg(args, i)); }

// Argument `i` as an unsigned integer.
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::uint32_t>(wholeArg(args, i));
}

// Argument `i` as a float.
float floatArg(std::span<const Value> args, std::size_t i) { return static_cast<float>(binding::number(args, i)); }

// Argument `i` as a boolean: nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].isNil()) {
        return false;
    }
    return binding::number(args, i) != 0.0 || args[i].type() != Value::Type::Number;
}

// Argument `i` as a boolean that is `fallback` when omitted.
bool boolArgOr(std::span<const Value> args, std::size_t i, bool fallback) {
    return i >= args.size() ? fallback : boolArg(args, i);
}

// A string argument that is empty for nil.
std::string nameArg(std::span<const Value> args, std::size_t i) {
    return i >= args.size() || args[i].isNil() ? std::string{} : binding::string(args, i);
}

// Registers one binding.
template <typename Body> void add(LuaVm& vm, std::string_view name, Body body) {
    vm.registerFunction(name, NativeFunction(std::move(body)));
}

// ---- The world and the effects ----

// `ObjMarkZone(zone, kind, removed)`: kind 0 only; every spawn record of the zone is marked removed, or with 0 brought
// back. **Coney's**: the stored-hidden bit and the store of a live hidden object are not modelled (Coney's records keep
// no stored objects).
// @orig 0x003967e0 ObjZone_Mark (unknown)
// @orig 0x003983b0 ObjZone_MarkRemoved (unknown)
void markZone(world_objects::SpawnRecords& records, std::uint32_t zone, bool removed) {
    for (const world_objects::SpawnRecord& record : records.all()) {
        if (record.zone != zone) {
            continue;
        }
        if (world_objects::SpawnRecord* found = records.find(record.handle); found != nullptr) {
            found->removed = removed;
            if (removed) {
                found->live = false;
            }
        }
    }
}

// `ResetStore(store)`: a store front flag (activity 14) loses its robbed bit, its byte 8-15 becomes 0xff and its gang
// field 31 (no gang). **Coney stand-in**: the alarm strobe within 7 m is not modelled, so its message 0x13 is not sent.
// @orig 0x0041ddd8 Store_Reset (unknown)
void resetStore(world_objects::WorldFlags& flags, double handle) {
    world_objects::WorldFlag* flag = flags.find(handle);
    if (flag == nullptr || flag->kind != kStoreActivity) {
        return;
    }
    auto group = static_cast<std::uint32_t>(flag->kind2);
    group = (group & ~kRobbedBit & ~kStoreGangMask) | kStoreByte | kStoreGangMask;
    flag->kind2 = static_cast<int>(group);
}

} // namespace

void addHubWorldBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context) {
    using A = std::span<const Value>;
    GameState& state = *context.state;

    // ---- The world and the effects.
    // `CarDestroy(car)`: gone at once, with no effect.
    // @orig 0x0038dea8 Car_Destroy (unknown)
    add(vm, "CarDestroy", [cars = context.cars](A args) {
        if (cars != nullptr) {
            static_cast<void>(cars->destroy(static_cast<double>(unsignedArg(args, 0))));
        }
        return binding::none();
    });
    // `FlagNetClear()`: the flag network's nodes all gone.
    // @orig 0x002a7328 FlagNet_Clear (unknown)
    add(vm, "FlagNetClear", [net = context.flagNet](A) {
        if (net != nullptr) {
            net->clear();
        }
        return binding::none();
    });
    add(vm, "ObjMarkZone", [records = context.spawnRecords](A args) {
        if (records != nullptr && intArg(args, 1) == 0) {
            markZone(*records, unsignedArg(args, 0), intArg(args, 2) != 0);
        }
        return binding::none();
    });
    // `KillParticle(particle)`: message 0x15 (destroy) to the object: a particle system ends, any other object that
    // takes the message (Coney's: a spawn record's object) is destroyed.
    // @orig 0x00397730 Particle_Kill (unknown)
    // @orig 0x003a2e00 Task_SendMessage (unknown)
    add(vm, "KillParticle", [context = &context](A args) {
        const auto handle = static_cast<double>(unsignedArg(args, 0));
        const bool particle = context->effects != nullptr && context->effects->particles.kill(handle);
        if (!particle && context->spawnRecords != nullptr) {
            static_cast<void>(context->spawnRecords->destroy(handle));
        }
        return binding::none();
    });
    // `SetMotionAlpha(alpha)`: the motion blur's strength at once. **Coney's**: the game-state check that can block it
    // (`0x0041d110`) is not traced, so it never blocks.
    // @orig 0x0040cc70 ScreenFx_SetMotionAlpha (unknown)
    add(vm, "SetMotionAlpha", [effects = context.effects](A args) {
        if (effects != nullptr) {
            effects->motionBlur.queueAlpha(
                static_cast<std::uint8_t>(std::min<std::uint32_t>(unsignedArg(args, 0), 255)), 0.0F);
        }
        return binding::none();
    });
    // `GetPTank(resource, pos, count, distance, capacity)`: a sprite batch's handle (its slot << 16). **Coney's**: the
    // slots count from 1, so no handle is 0 (Coney's NilHandle).
    // @orig 0x0039c388 PTank_Create (unknown)
    add(vm, "GetPTank", [&state](A) {
        std::vector<bool>& slots = state.hub.ptanks;
        std::size_t slot = 1;
        while (slot < slots.size() && slots.at(slot)) {
            ++slot;
        }
        if (slot >= slots.size()) {
            slots.resize(slot + 1, false);
        }
        slots.at(slot) = true;
        return binding::number(static_cast<double>(slot << kPTankShift));
    });
    // `ReleasePTank(ptank)`: frees the batch's slot.
    // @orig 0x0039c3d0 PTank_Release (unknown)
    add(vm, "ReleasePTank", [&state](A args) {
        const std::size_t slot = unsignedArg(args, 0) >> kPTankShift;
        if (slot < state.hub.ptanks.size()) {
            state.hub.ptanks.at(slot) = false;
        }
        return binding::none();
    });

    // ---- The HUD.
    // @orig 0x001b5ea8 HUD_SetActionTextHigh (unknown)
    add(vm, "HUDEnableClubActionText", [hud = context.hud](A args) {
        if (hud != nullptr) {
            hud->setClubActionText(boolArg(args, 0));
        }
        return binding::none();
    });
    // `HUDTurnOnActionCycleAnim(framesPerIcon, seconds, blinkFrames, iconA, iconB, player)`: the seconds are ignored.
    // **Coney choice**: a frame count of 0 (which crashes the original's update) is kept as 1.
    // @orig 0x001b5a90 HUD_TurnOnActionCycleAnim (unknown)
    add(vm, "HUDTurnOnActionCycleAnim", [hud = context.hud](A args) {
        const std::size_t player = unsignedArg(args, 5);
        if (hud != nullptr && player < kPromptPlayers) {
            hud->startActionCycle(player, hud::ActionCycle{.on = true,
                                                           .framesPerIcon = std::max(unsignedArg(args, 0), 1U),
                                                           .blinkFrames = unsignedArg(args, 2),
                                                           .iconA = unsignedArg(args, 3),
                                                           .iconB = unsignedArg(args, 4)});
        }
        return binding::none();
    });
    // @orig 0x001b5ae0 HUD_TurnOffActionCycleAnim (unknown)
    add(vm, "HUDTurnOffActionCycleAnim", [hud = context.hud](A args) {
        const std::size_t player = unsignedArg(args, 0);
        if (hud != nullptr && player < kPromptPlayers) {
            hud->stopActionCycle(player);
        }
        return binding::none();
    });
    // `HUDShowMissionSelect(onCancel, onChoose)`: the names are kept to 31 characters.
    // @orig 0x00155180 MissionSelect_Show (unknown)
    add(vm, "HUDShowMissionSelect", [host = context.host](A args) {
        std::string onCancel = nameArg(args, 0);
        std::string onChoose = nameArg(args, 1);
        onCancel.resize(std::min<std::size_t>(onCancel.size(), 31));
        onChoose.resize(std::min<std::size_t>(onChoose.size(), 31));
        if (host != nullptr) {
            host->showMissionSelect(onCancel, onChoose);
        }
        return binding::none();
    });
    // @orig 0x001551e0 GameStats_Show (unknown)
    add(vm, "ShowGameStatsInterface", [host = context.host](A args) {
        if (host != nullptr) {
            host->showGameStats(nameArg(args, 0));
        }
        return binding::none();
    });

    // ---- The sound.
    // `EnableAmbientEmitter(emitter, on)`: on by default.
    // @orig 0x00113bc8 Audio_EnableAmbientEmitter (unknown)
    add(vm, "EnableAmbientEmitter", [context = &context](A args) {
        if (context->sound != nullptr) {
            context->sound->enableAmbientEmitter(intArg(args, 0), boolArgOr(args, 1, true));
        }
        return binding::none();
    });
    // `SndLoadMatrix(name)`: a new name empties the game's sound matrix and runs `<name>_preload.lua`. The matrix is
    // the sound's (it starts named `sound`, so the scripts' own `SndLoadMatrix("sound")` changes nothing); without
    // sound the name is kept here.
    // @orig 0x00113490 Audio_LoadMatrix (unknown)
    add(vm, "SndLoadMatrix", [&scripts, &state, context = &context](A args) {
        const std::string name = binding::string(args, 0);
        const bool changed =
            context->sound != nullptr ? context->sound->loadSoundMatrix(name) : name != state.hub.soundMatrix;
        state.hub.soundMatrix = name;
        if (!changed) {
            return binding::none();
        }
        static_cast<void>(scripts.runFile(std::vformat(kMatrixPreload, std::make_format_args(name))));
        return binding::none();
    });
    // `SoundPlay(sound, pos)`: once at a point, the handle back (NilSoundHandle 0 when it did not start).
    // @orig 0x00113680 Audio_PlaySoundAt (unknown)
    add(vm, "SoundPlay", [context = &context](A args) {
        const std::array<float, 3> at = binding::position(args, 1).value_or(std::array<float, 3>{});
        const double handle =
            context->sound != nullptr ? context->sound->play3D(crc32(binding::string(args, 0)), at) : 0.0;
        return binding::number(handle);
    });

    // ---- The level and the game state.
    // `CheckMultiplayer()`: the players in line with the two-player setting. **Coney stand-in**: Coney has one player
    // and no second pad, so there is never a second player to make or to give back; nothing changes.
    // @orig 0x0041dd90 Game_CheckMultiplayer (unknown)
    add(vm, "CheckMultiplayer", [](A) { return binding::none(); });
    add(vm, "ResetStore", [flags = context.flags](A args) {
        if (flags != nullptr) {
            resetStore(*flags, static_cast<double>(unsignedArg(args, 0)));
        }
        return binding::none();
    });
    // `SetLUASaveDataBool(flag, on)` and `GetLUASaveDataBool(flag)`: flags 1-128 are the saved ones; others are kept
    // unsaved (HubState::unsavedFlags).
    // @orig 0x0041ad28 GameState_SetLuaSaveBool (unknown)
    add(vm, "SetLUASaveDataBool", [&state](A args) {
        const int flag = intArg(args, 0);
        const bool on = boolArg(args, 1);
        if (flag >= 1 && flag <= kSavedFlags) {
            state.saved.setScriptFlag(static_cast<std::size_t>(flag), on);
        } else {
            state.hub.unsavedFlags[flag] = on;
        }
        return binding::none();
    });
    // @orig 0x0041ad60 GameState_GetLuaSaveBool (unknown)
    add(vm, "GetLUASaveDataBool", [&state](A args) {
        const int flag = intArg(args, 0);
        if (flag >= 1 && flag <= kSavedFlags) {
            return binding::boolean(state.saved.scriptFlag(static_cast<std::size_t>(flag)));
        }
        const auto found = state.hub.unsavedFlags.find(flag);
        return binding::boolean(found != state.hub.unsavedFlags.end() && found->second);
    });
    // @orig 0x00155308 Autosave_Request (unknown)
    add(vm, "SSMC_StartSaveSequence", [host = context.host](A) {
        if (host != nullptr) {
            host->startSaveSequence();
        }
        return binding::none();
    });

    // ---- The configuration.
    // `CfgActionDistance(action, metres)`: stored squared; an action outside 0-5 is ignored (**Coney choice**: the
    // original does not check it).
    // @orig 0x00417af0 Cfg_SetActionDistance (unknown)
    add(vm, "CfgActionDistance", [&state](A args) {
        const std::uint32_t action = unsignedArg(args, 0);
        if (action < kActionKinds) {
            const float metres = floatArg(args, 1);
            state.hub.actionDistanceSquared.at(action) = metres * metres;
        }
        return binding::none();
    });
    // `CfgEnableCrimeType(crime, enabled)`: the crime type's byte (only type 12 is read back, by the police).
    // @orig 0x0041da80 Cfg_EnableCrimeType (unknown)
    add(vm, "CfgEnableCrimeType", [&state](A args) {
        const int crime = intArg(args, 0);
        if (crime >= 0 && crime < kCrimeTypes) {
            state.player.crimes.setEnabled(crime, boolArg(args, 1));
        }
        return binding::none();
    });
    // @orig 0x0041da50 Cfg_SetTurfInvasion (unknown)
    add(vm, "CfgEnableTurfInvasion", [&state](A args) {
        state.hub.turfInvasion = boolArgOr(args, 0, true);
        return binding::none();
    });
    // @orig 0x0041d618 GameState_SetObjectValueMod (unknown)
    add(vm, "CfgObjectValueMod", [&state](A args) {
        state.hub.objectValueFactor = floatArg(args, 0);
        return binding::none();
    });
    // @orig 0x00236480 Cfg_SetPlayerCombatWalkOnly (unknown)
    add(vm, "CfgPlayerCombatWalkOnly", [&state](A args) {
        state.hub.playerCombatWalkOnly = boolArg(args, 0);
        return binding::none();
    });
    // `CfgStickDeflection(creep, run)`: only the run's deflection is kept: the stick's magnitude above which a human
    // runs (`0x005102e8`, human::LocomotionTuning::runThreshold).
    // @orig 0x00236470 Cfg_SetStickDeflection (unknown)
    add(vm, "CfgStickDeflection", [](A args) {
        human::locomotionTuning().runThreshold = floatArg(args, 1);
        return binding::none();
    });
}

} // namespace coney::script
