// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/arena_bindings.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "human/human_flags.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/human_bindings.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

namespace coney::script {

namespace {

// `Teleport`'s heading that keeps the object's rotation.
constexpr int kKeepHeading = -1;
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

// `Teleport(object, {x, y, z}, heading)`: moves a human the scripts made, as `TeleportToFlag` does (a running level
// moves it, the player too); heading -1 (the default) keeps its facing. **Coney stand-in**: only humans are kept, so
// another object's handle does nothing, and the human is placed at the point as given (the original lowers it onto
// what a short ray down finds).
// @orig 0x00385bb8 Object_Teleport (unknown)
NativeFunction makeTeleport(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const double handle = handleArg(args, 0);
        HumanCreation* human = context->humans != nullptr ? context->humans->find(handle) : nullptr;
        if (human == nullptr || args.size() < 2 || args[1].table() == nullptr) {
            return binding::none();
        }
        const Table& table = *args[1].table();
        world_objects::Placement placement;
        for (std::size_t k = 0; k < placement.position.size(); ++k) {
            placement.position.at(k) =
                static_cast<float>(table.get(Value(static_cast<double>(k + 1))).number().value_or(0.0));
        }
        // The facing it has now: where the level has it, else where it was made or last teleported.
        const int heading = intArg(args, 2, kKeepHeading);
        if (heading == kKeepHeading) {
            std::optional<world_objects::Placement> now =
                context->ai != nullptr ? context->ai->humanPlacement(handle) : std::nullopt;
            if (!now) {
                now = context->humans->placement(handle);
            }
            placement.headingDegrees = now ? now->headingDegrees : 0.0F;
        } else {
            placement.headingDegrees = static_cast<float>(heading);
        }
        human->teleported = placement;
        ++human->teleports;
        if (context->ai != nullptr) {
            context->ai->humanTeleported(handle, placement);
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

// Registers the humans' and gangs' bindings.
void addCharacterBindings(LuaVm& vm, const BindingContext& context) {
    // `HuLockMovement(human, lock)`.
    // @orig 0x00234ef8 Human_LockMovement (unknown)
    vm.registerFunction("HuLockMovement", humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
                            host.setFlags(handleArg(args, 0), human::flag::kMovementLocked, boolArg(args, 1));
                        }));
    // `HuEnableSoundCommands(human, enable)`: enable defaults to true.
    // @orig 0x00239240 Human_EnableSoundCommands (unknown)
    vm.registerFunction("HuEnableSoundCommands",
                        humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
                            host.setSoundCommands(handleArg(args, 0), boolArg(args, 1, true));
                        }));
    // `HuPutItemInPocket(human, item, count)`: count defaults to 1, kept as a byte; item 0 empties the pocket.
    // @orig 0x00238190 Human_SetPocketItem (unknown)
    vm.registerFunction("HuPutItemInPocket",
                        humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
                            const int item = intArg(args, 1);
                            const int count = item == 0 ? 0 : (intArg(args, 2, 1) & kMaxPocketCount);
                            host.setPocket(handleArg(args, 0), item, count);
                        }));
    // `HuRemoveItemInPocket(human)`.
    // @orig 0x002381f0 Human_RemoveItemInPocket (unknown)
    vm.registerFunction("HuRemoveItemInPocket",
                        humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
                            host.setPocket(handleArg(args, 0), 0, 0);
                        }));
    // `BrSetDamageResponse(human, response)`: kept as a 16-bit signed value.
    // @orig 0x00292758 Brain_SetDamageResponse (unknown)
    vm.registerFunction("BrSetDamageResponse",
                        humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
                            host.setDamageResponse(handleArg(args, 0), static_cast<std::int16_t>(intArg(args, 1)));
                        }));
    // `GangSetDamageResponse(gang, response)`: -1 does nothing.
    // @orig 0x0016b460 Gang_SetDamageResponse (unknown)
    vm.registerFunction("GangSetDamageResponse",
                        humanCall(context, [](HumanBindingHost& host, std::span<const Value> args) {
                            if (const int gang = intArg(args, 0); gang != -1) {
                                host.setGangDamageResponse(gang, static_cast<std::int16_t>(intArg(args, 1)));
                            }
                        }));
    vm.registerFunction("Teleport", makeTeleport(context));
}

} // namespace

void addArenaBindings(LuaVm& vm, const BindingContext& context) {
    addStateBindings(vm, context);
    addCharacterBindings(vm, context);
}

} // namespace coney::script
