// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/arena_bindings.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string>

#include "camera/cameras.h"
#include "characters/character_class.h"
#include "human/human_flags.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/human_bindings.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"
#include "world_objects/spawn_records.h"
#include "world_objects/spinning_icons.h"

namespace coney::script {

namespace {

// `Teleport`'s heading that keeps the object's rotation.
constexpr int kKeepHeading = -1;
// `TacticDomination`'s range when none is given, metres.
constexpr float kDominationRange = 3.0F;
// The largest pocket count: a byte (`+0x254`).
constexpr int kMaxPocketCount = 255;
// The game modes that set the flag at `+0x56f0`.
constexpr std::uint32_t kVersusMode1 = 1;
constexpr std::uint32_t kVersusMode2 = 2;

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Whether argument `i` is missing or nil, so a default applies.
bool absent(std::span<const Value> args, std::size_t i) { return i >= args.size() || args[i].isNil(); }

// Argument `i` as an integer, `fallback` when omitted.
int intArg(std::span<const Value> args, std::size_t i, int fallback = 0) {
    return i >= args.size() ? fallback : static_cast<int>(wholeArg(args, i));
}

// Argument `i` as an unsigned integer (kept to 32 bits).
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::uint32_t>(wholeArg(args, i));
}

// A handle argument: truncated to an unsigned integer.
double handleArg(std::span<const Value> args, std::size_t i) { return static_cast<double>(unsignedArg(args, i)); }

// Argument `i` as a boolean: nil and 0 are false; `fallback` when it is not passed at all.
bool boolArg(std::span<const Value> args, std::size_t i, bool fallback = false) {
    if (i >= args.size()) {
        return fallback;
    }
    if (args[i].isNil()) {
        return false;
    }
    return args[i].type() != Value::Type::Number || binding::number(args, i) != 0.0;
}

// The character host of the level, if there is a level with an AI host.
HumanBindingHost* humansOf(const BindingContext& context) {
    return context.ai != nullptr ? context.ai->humans() : nullptr;
}

// A binding that hands its arguments to the AI host when there is one and returns nothing.
template <typename Body> NativeFunction aiCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        if (AiBindingHost* host = context->ai; host != nullptr) {
            body(*host, args);
        }
        return binding::none();
    };
}

// A binding that hands its arguments to the character host when there is one and returns nothing. The host is read
// at each call: a level gives its brains to a Lua state made before it.
template <typename Body> NativeFunction humanCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        if (HumanBindingHost* host = humansOf(*context); host != nullptr) {
            body(*host, args);
        }
        return binding::none();
    };
}

// A binding that changes the game state with `body` and returns nothing.
template <typename Body> NativeFunction stateCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        if (context->state != nullptr) {
            body(*context->state, args);
        }
        return binding::none();
    };
}

// `SetGameMode(mode, a, b, gangSize)`: modes 1 and 2 set the flag at `+0x56f0`, every other clears it.
// @orig 0x0041d788 GameState_SetGameMode (unknown)
NativeFunction makeSetGameMode(const BindingContext& context) {
    return stateCall(context, [](GameState& state, std::span<const Value> args) {
        const std::uint32_t mode = unsignedArg(args, 0);
        state.gameMode = GameModeSetting{.mode = mode,
                                         .a = unsignedArg(args, 1),
                                         .b = unsignedArg(args, 2),
                                         .gangSize = unsignedArg(args, 3),
                                         .versus = mode == kVersusMode1 || mode == kVersusMode2};
    });
}

// `GetGameMode()`: 0 without a game state, as in the story.
// @orig 0x0041d7e0 GameState_GetGameMode (unknown)
NativeFunction makeGetGameMode(const BindingContext& context) {
    return [state = context.state](std::span<const Value>) {
        return binding::number(state != nullptr ? static_cast<double>(state->gameMode.mode) : 0.0);
    };
}

// `Teleport(object, {x, y, z}, heading)`: moves a human the scripts made (a running level moves it, the player too),
// or a spawned object (its spawn record, which the trigger spheres and the drawing read); heading -1 (the default)
// keeps its facing; another turns it about -z, clockwise seen from above, as `TeleportToFlag` does
// (docs/research/flags.md#position). For a human it is the placement alone (TeleportMotion::Kept: no motion reset,
// docs/research/teleports.md#motion-reset); the binding records the point as given, and the running level's
// placement lowers it onto what the 0.5 m ray down finds (Human::place()).
// @orig 0x00385bb8 Object_Teleport (unknown)
NativeFunction makeTeleport(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const double handle = handleArg(args, 0);
        if (args.size() < 2 || args[1].table() == nullptr) {
            return binding::none();
        }
        const Table& table = *args[1].table();
        world_objects::Placement placement;
        for (std::size_t k = 0; k < placement.position.size(); ++k) {
            placement.position.at(k) =
                static_cast<float>(table.get(Value(static_cast<double>(k + 1))).number().value_or(0.0));
        }
        const int heading = intArg(args, 2, kKeepHeading);
        HumanCreation* human = context->humans != nullptr ? context->humans->find(handle) : nullptr;
        if (human == nullptr) {
            // Another object (a prop, an effect) takes the transform as given: its spawn record's position, and a
            // rotation about -z (Quat_FromAxisAngle with the axis (0, 0, -1)) unless the heading is -1.
            world_objects::SpawnRecord* record =
                context->spawnRecords != nullptr ? context->spawnRecords->find(handle) : nullptr;
            if (record != nullptr && !record->removed) {
                record->position = placement.position;
                if (heading != kKeepHeading) {
                    const float half = static_cast<float>(heading) * std::numbers::pi_v<float> / 360.0F;
                    record->rotation = {0.0F, 0.0F, -std::sin(half), std::cos(half)};
                }
            }
            return binding::none();
        }
        // The facing it has now: where the level has it, else where it was made or last teleported.
        if (heading == kKeepHeading) {
            std::optional<world_objects::Placement> now =
                context->ai != nullptr ? context->ai->humanPlacement(handle) : std::nullopt;
            if (!now) {
                now = context->humans->placement(handle);
            }
            placement.headingDegrees = now ? now->headingDegrees : 0.0F;
        } else {
            placement.headingDegrees = world_objects::placementHeading(static_cast<float>(heading));
        }
        // Object_Teleport makes the placement alone: the human's move goes on (docs/research/teleports.md).
        human->teleported = placement;
        human->teleportMotion = world_objects::TeleportMotion::Kept;
        ++human->teleports;
        if (context->ai != nullptr) {
            context->ai->humanTeleported(handle, placement, world_objects::TeleportMotion::Kept);
        }
        return binding::none();
    };
}

// Registers the game state's bindings.
void addStateBindings(LuaVm& vm, const BindingContext& context) {
    vm.registerFunction("SetGameMode", makeSetGameMode(context));
    vm.registerFunction("GetGameMode", makeGetGameMode(context));
    // `CNSEnableMissionInfo(on)`: nothing reads the word it sets.
    // @orig 0x0023b128 CNS_SetMissionInfoEnabled (unknown)
    vm.registerFunction("CNSEnableMissionInfo", stateCall(context, [](GameState& state, std::span<const Value> args) {
                            state.missionInfo = boolArg(args, 0);
                        }));
    // `WCEnableAutomaticSwitching(on)`: on defaults to true.
    // @orig 0x0041dd68 GameState_SetAutoSwitch (unknown)
    vm.registerFunction("WCEnableAutomaticSwitching",
                        stateCall(context, [](GameState& state, std::span<const Value> args) {
                            state.autoSwitch = boolArg(args, 0, true);
                        }));
    // `EnableGameOverCheck(on)`: missing counts as false.
    // @orig 0x0041d890 GameState_EnableGameOverCheck (unknown)
    vm.registerFunction("EnableGameOverCheck", stateCall(context, [](GameState& state, std::span<const Value> args) {
                            state.gameOver.setEnabled(boolArg(args, 0));
                        }));
    // `QueueFileToPrecache(file)`: appended unchecked; a missing file is dropped when the queue is loaded.
    // @orig 0x0040cc40 World_QueuePackToPrecache (unknown)
    vm.registerFunction("QueueFileToPrecache", stateCall(context, [](GameState& state, std::span<const Value> args) {
                            if (!absent(args, 0)) {
                                state.precacheQueue.push_back(binding::string(args, 0));
                            }
                        }));
    // `PrecacheWorld(budgetMs, radius, pack)`: the preload empties the queue. **Coney stand-in**: Coney loads the
    // whole level at once and a character's pack when a human first needs it, so there is nothing to stream or load
    // here, and the game clock is not paused.
    // @orig 0x0040c948 World_Precache (unknown)
    vm.registerFunction("PrecacheWorld", stateCall(context, [](GameState& state, std::span<const Value>) {
                            state.precacheQueue.clear();
                        }));
}

// The humans' and gangs' bindings, one function each, so that each port is compared on its own.

// `HuLockMovement(human, lock)`.
// @orig 0x00234ef8 Human_SetLockMovement (unknown)
NativeFunction makeLockMovement(const BindingContext& context) {
    return humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setFlags(handleArg(args, 0), human::flag::kMovementLocked, boolArg(args, 1));
    });
}

// `HuEnableSoundCommands(human, enable)`: enable defaults to true.
// @orig 0x00239240 Human_EnableSoundCommands (unknown)
NativeFunction makeEnableSoundCommands(const BindingContext& context) {
    return humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setSoundCommands(handleArg(args, 0), boolArg(args, 1, true));
    });
}

// `HuPutItemInPocket(human, item, count)`: count defaults to 1, kept as a byte; item 0 empties the pocket.
// @orig 0x00238190 Human_SetPocketItem (unknown)
NativeFunction makePutItemInPocket(const BindingContext& context) {
    return humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        const int item = intArg(args, 1);
        const int count = item == 0 ? 0 : (intArg(args, 2, 1) & kMaxPocketCount);
        host.setPocket(handleArg(args, 0), item, count);
    });
}

// `HuRemoveItemInPocket(human)`.
// @orig 0x002381f0 Human_RemoveItemInPocket (unknown)
NativeFunction makeRemoveItemInPocket(const BindingContext& context) {
    return humanCall(
        context, [](HumanBindingHost& host, std::span<const Value> args) { host.setPocket(handleArg(args, 0), 0, 0); });
}

// `BrSetDamageResponse(human, response)`: kept as a 16-bit signed value.
// @orig 0x00292758 Brain_SetDamageResponse (unknown)
NativeFunction makeBrSetDamageResponse(const BindingContext& context) {
    return humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setDamageResponse(handleArg(args, 0), static_cast<std::int16_t>(intArg(args, 1)));
    });
}

// `GangSetDamageResponse(gang, response)`: -1 does nothing; otherwise each member the gang has now.
// @orig 0x0016b460 Gang_SetDamageResponseById (unknown)
NativeFunction makeGangSetDamageResponse(const BindingContext& context) {
    return humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        if (const int gang = intArg(args, 0); gang != -1) {
            host.setGangDamageResponse(gang, static_cast<std::int16_t>(intArg(args, 1)));
        }
    });
}

// `TurnWarriorCommands(on)`: every Warrior command of every player on (non-zero) or off. Not ported yet (#1726): the
// original hands it to `WCEnableAllCommands`, which skips a player whose human is not there and also sets whether the
// command display may open.
// @orig 0x0041c2b0 GameState_TurnWarriorCommands (unknown)
NativeFunction makeTurnWarriorCommands(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->state == nullptr) {
            return binding::none();
        }
        const bool on = intArg(args, 0) != 0;
        for (auto& player : context->state->characters.warriorCommands) {
            player.fill(on);
        }
        if (HumanBindingHost* host = humansOf(*context); host != nullptr) {
            host->applyRules(context->state->characters);
        }
        return binding::none();
    };
}

// `GangAttachSpinningIcon(gang, icon, bone)`: -1 does nothing; each member gets world_objects::iconTypeName(icon).
// @orig 0x0016b2c8 Gang_ShowMembersIcon (unknown)
NativeFunction makeGangAttachSpinningIcon(const BindingContext& context) {
    return humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        if (const int gang = intArg(args, 0); gang != -1) {
            host.setGangIcon(gang, world_objects::iconTypeName(binding::string(args, 1)), intArg(args, 2));
        }
    });
}

// `GangRemoveSpinningIcon(gang)`: -1 does nothing.
// @orig 0x0016b358 Gang_RemoveMembersIcon (unknown)
NativeFunction makeGangRemoveSpinningIcon(const BindingContext& context) {
    return humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        if (const int gang = intArg(args, 0); gang != -1) {
            host.setGangIcon(gang, {}, 0);
        }
    });
}

// `HuForceEnableReticule(human, enable)`: one global; a handle that names no human leaves it.
// @orig 0x00236978 Human_ForceEnableReticule (unknown)
NativeFunction makeForceEnableReticule(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        HumanBindingHost* host = humansOf(*context);
        if (host != nullptr && context->state != nullptr && host->status(handleArg(args, 0)).has_value()) {
            context->state->forceReticules = boolArg(args, 1);
        }
        return binding::none();
    };
}

// `GoalMoveToHuman(human, target, gait, radius)`.
// @orig 0x002dc458 Goal_MoveToHuman (unknown)
NativeFunction makeGoalMoveToHuman(const BindingContext& context) {
    return aiCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.goalMoveToHuman(handleArg(args, 0), handleArg(args, 1), intArg(args, 2),
                             static_cast<float>(binding::number(args, 3)));
    });
}

// `GoalEngageEnemy(human, enemy)`.
// @orig 0x002af528 Goal_EngageEnemy (unknown)
NativeFunction makeGoalEngageEnemy(const BindingContext& context) {
    return aiCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.goalEngageEnemy(handleArg(args, 0), handleArg(args, 1));
    });
}

// `BrSetType(human, type)`: the types the host takes (ScriptedBrains::brSetType()).
// @orig 0x00292410 Brain_SetType (unknown)
NativeFunction makeBrSetType(const BindingContext& context) {
    return aiCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.brSetType(handleArg(args, 0), intArg(args, 1));
    });
}

// `HuGetCharType(human)`: the behaviour class of the type the human was made as (characters::characterClassOf());
// 0 for a handle that names no human.
// @orig 0x00235890 Human_GetCharType (unknown)
NativeFunction makeGetCharType(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const HumanCreation* human = context->humans != nullptr ? context->humans->find(handleArg(args, 0)) : nullptr;
        return binding::number(human != nullptr ? characters::characterClassOf(human->type).id : 0);
    };
}

// `HuSetNoAutoLock(human, on)`: the flag only (no reader is on the page).
// @orig 0x002340a8 Human_SetNoAutoLock (unknown)
NativeFunction makeSetNoAutoLock(const BindingContext& context) {
    return humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
        host.setFlags(handleArg(args, 0), human::flag::kNoAutoLock, boolArg(args, 1));
    });
}

// `HuSetWheelchairControl(human, on)`, for a handle that names a human: on, the cameras' look-behind switch (11) off
// and, unless he is in the chair already, the wheelchair flag (which gives a pad-driven human the wheelchair's
// control, Human::updateState()) and every player command off; off, the switch on, the flag cleared and, only when he
// was in the chair, every command on again (docs/research/characters-locomotion.md#wheelchair).
// @orig 0x00234188 Human_SetWheelchairControl (unknown)
NativeFunction makeSetWheelchairControl(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        constexpr int kAllCommands = 0;
        const double human = handleArg(args, 0);
        const bool on = boolArg(args, 1);
        HumanBindingHost* host = humansOf(*context);
        const std::optional<HumanStatus> status = host != nullptr ? host->status(human) : std::nullopt;
        if (!status) {
            return binding::none();
        }
        if (context->cameras != nullptr) {
            context->cameras->enable(camera::Cameras::kSwitchLookBehind, !on);
        }
        // Coney stand-in: the wheelchair flag, read before it is cleared, for "runs the wheelchair control" (the
        // control pointer +0x1c0), as Coney picks that control from the flag.
        const bool inChair = (status->flags & human::flag::kWheelchair) != 0;
        if (on && !inChair) {
            // Stand-in: the flag stands for the control saved and the wheelchair's set (Human_SaveControl,
            // Human_SetWheelchairMoveControl); commands 46 and 47 are not added, as the control reads L1 and R1 itself.
            host->setFlags(human, human::flag::kWheelchair, true);
            host->enableCommand(human, kAllCommands, false);
        } else if (!on) {
            host->setFlags(human, human::flag::kWheelchair, false);
            if (inChair) {
                // Stand-in: the flag cleared stands for the saved control's return (Human_RestoreControl); there are
                // no commands 46 and 47 to remove.
                host->enableCommand(human, kAllCommands, true);
            }
        }
        return binding::none();
    };
}

// One byte of `ObjColor`'s tint: the component times 255 (Obj_SetColour), times 255 again (Colour_PackFloat), turned
// into an unsigned integer as Float_ToUInt does (0 for a negative value or NaN, all ones from 2^32) and kept to its
// low 8 bits. Since 65,025 is 1 modulo 256, a whole component 0-255 comes out as itself.
std::uint32_t colourByte(float component) {
    constexpr float kScale = 255.0F;
    constexpr float kAllOnesFrom = 4294967296.0F;
    constexpr std::uint32_t kByte = 0xffU;
    const float scaled = (component * kScale) * kScale;
    if (!(scaled > 0.0F)) {
        return 0;
    }
    if (scaled >= kAllOnesFrom) {
        return kByte;
    }
    return static_cast<std::uint32_t>(scaled) & kByte;
}

// `ObjColor(object, {r, g, b, a})`: the four components read as floats (0 for a missing one), a spawned object's tint
// word `0xRRGGBBAA` made of them (colourByte()), then the four floats written back into the table, whether or not the
// handle names an object (docs/research/objects.md#tint).
// @orig 0x00378088 lua_ObjColor (unknown)
// @orig 0x00396bd0 Obj_SetColour (unknown)
NativeFunction makeObjColor(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        constexpr std::size_t kComponents = 4;
        constexpr std::uint32_t kBitsPerComponent = 8;
        if (args.size() < 2 || args[1].table() == nullptr) {
            return binding::none();
        }
        const double handle = handleArg(args, 0);
        Table& table = *args[1].table();
        std::array<float, kComponents> components{};
        for (std::size_t k = 0; k < kComponents; ++k) {
            components.at(k) = static_cast<float>(table.get(Value(static_cast<double>(k + 1))).number().value_or(0.0));
        }
        // Coney stand-in: only a spawned object keeps a tint; the words +0xc8 and +0xcc of another object (a human)
        // are not kept.
        world_objects::SpawnRecord* record =
            context->spawnRecords != nullptr ? context->spawnRecords->find(handle) : nullptr;
        if (record != nullptr) {
            std::uint32_t word = 0;
            for (const float component : components) {
                word = (word << kBitsPerComponent) | colourByte(component);
            }
            record->tint = word;
        }
        for (std::size_t k = 0; k < kComponents; ++k) {
            static_cast<void>(
                table.set(Value(static_cast<double>(k + 1)), Value(static_cast<double>(components.at(k)))));
        }
        return binding::none();
    };
}

// `ActGiveWay(human, other)`: the human steps out of the other's way (docs/research/ai-moving.md#giving-way).
// @orig 0x003648a0 lua_ActGiveWay (unknown)
NativeFunction makeActGiveWay(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (AiBindingHost* ai = context->ai; ai != nullptr) {
            ai->actGiveWay(handleArg(args, 0), handleArg(args, 1));
        }
        return binding::none();
    };
}

// `TacticDomination(gang, flag, range, callback)`: range defaults to 3 m; a nil callback is none.
// @orig 0x00316b30 Tactic_Domination (unknown)
NativeFunction makeTacticDomination(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (AiBindingHost* ai = context->ai; ai != nullptr) {
            const float range = absent(args, 2) ? kDominationRange : static_cast<float>(binding::number(args, 2));
            const std::string callback = absent(args, 3) ? std::string{} : binding::string(args, 3);
            ai->tacticDomination(intArg(args, 0), handleArg(args, 1), range, callback);
        }
        return binding::none();
    };
}

// Registers the humans' and gangs' bindings.
void addCharacterBindings(LuaVm& vm, const BindingContext& context) {
    vm.registerFunction("HuLockMovement", makeLockMovement(context));
    vm.registerFunction("HuEnableSoundCommands", makeEnableSoundCommands(context));
    vm.registerFunction("HuPutItemInPocket", makePutItemInPocket(context));
    vm.registerFunction("HuRemoveItemInPocket", makeRemoveItemInPocket(context));
    vm.registerFunction("BrSetDamageResponse", makeBrSetDamageResponse(context));
    vm.registerFunction("GangSetDamageResponse", makeGangSetDamageResponse(context));
    vm.registerFunction("Teleport", makeTeleport(context));
    // `HuSetConscious(human, conscious)`: the port of Human_SetConscious (0x00237778) is
    // ScriptedHumans::setConscious().
    vm.registerFunction("HuSetConscious", humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
                            host.setConscious(handleArg(args, 0), boolArg(args, 1));
                        }));
    // `HuSetSlowMo(fraction, human)`: the whole game's characters' step. **Coney stand-in**: the player's byte
    // `+0x3bb` it marks is not kept, as its reader is not on the page.
    vm.registerFunction("HuSetSlowMo", [context = &context](std::span<const Value> args) {
        if (context->cameras != nullptr) {
            context->cameras->slowMotion().setScripted(static_cast<float>(binding::number(args, 0)));
        }
        return binding::none();
    });
    vm.registerFunction("TurnWarriorCommands", makeTurnWarriorCommands(context));
    vm.registerFunction("GangAttachSpinningIcon", makeGangAttachSpinningIcon(context));
    vm.registerFunction("GangRemoveSpinningIcon", makeGangRemoveSpinningIcon(context));
    vm.registerFunction("HuForceEnableReticule", makeForceEnableReticule(context));
    vm.registerFunction("GoalMoveToHuman", makeGoalMoveToHuman(context));
    vm.registerFunction("GoalEngageEnemy", makeGoalEngageEnemy(context));
    vm.registerFunction("BrSetType", makeBrSetType(context));
    // `BrSetAttackWeight(human, attack, weight)`: the weight read as an unsigned integer, kept to a byte.
    vm.registerFunction("BrSetAttackWeight", aiCall(context, [](AiBindingHost& host, std::span<const Value> args) {
                            host.brSetAttackWeight(handleArg(args, 0), intArg(args, 1),
                                                   static_cast<int>(unsignedArg(args, 2) & 0xffU));
                        }));
    vm.registerFunction("HuGetCharType", makeGetCharType(context));
    vm.registerFunction("HuSetNoAutoLock", makeSetNoAutoLock(context));
    vm.registerFunction("HuSetWheelchairControl", makeSetWheelchairControl(context));
    vm.registerFunction("ObjColor", makeObjColor(context));
    vm.registerFunction("ActGiveWay", makeActGiveWay(context));
    vm.registerFunction("TacticDomination", makeTacticDomination(context));
}

} // namespace

void addArenaBindings(LuaVm& vm, const BindingContext& context) {
    addStateBindings(vm, context);
    addCharacterBindings(vm, context);
}

} // namespace coney::script
