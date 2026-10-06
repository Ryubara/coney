// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/hub_bindings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "effects/level_effects.h"
#include "human/human_flags.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/human_bindings.h"
#include "scripting/message_handlers.h"
#include "scripting/player_bindings.h"
#include "scripting/sound_bindings.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "warriors/inventory.h"
#include "world_objects/cars.h"
#include "world_objects/flags.h"
#include "world_objects/spawn_records.h"

namespace coney::script {

namespace {

// Coney's NilHandle (scripting/script_bindings.cpp), and the original's (the omitted handle a tolua default gives).
constexpr double kNilHandle = 0.0;
constexpr double kOmittedHandle = 4294967295.0;
// `HuGetVoiceIndex`'s answer for an invalid handle: -1 as an unsigned number.
constexpr double kNoVoice = 4294967295.0;
// A name the human keeps: 15 characters (a 16-byte field).
constexpr std::size_t kHumanNameLength = 15;
// `GetObjectName`'s answer for a handle that names no world object.
constexpr std::string_view kNullName = "<null>";
// The unlockables `HuAttachGear` asks (type 6, the upgrades): 5 the brass knuckles, 6 the steel-toe boots.
constexpr std::uint8_t kUpgradeType = 6;
constexpr std::uint32_t kKnucklesUpgrade = 5;
constexpr std::uint32_t kBootsUpgrade = 6;
// `GoalShopkeeper`'s default range, m.
constexpr float kDefaultShopRange = 9.0F;
// `BrCanUseWorldFlags`' default chance, percent.
constexpr int kDefaultWorldFlagChance = 10;

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as an integer.
int intArg(std::span<const Value> args, std::size_t i) { return static_cast<int>(wholeArg(args, i)); }

// Argument `i` as a float.
float floatArg(std::span<const Value> args, std::size_t i) { return static_cast<float>(binding::number(args, i)); }

// Whether argument `i` is missing or nil.
bool absent(std::span<const Value> args, std::size_t i) { return i >= args.size() || args[i].isNil(); }

// Argument `i` as a boolean: nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (absent(args, i)) {
        return false;
    }
    return binding::number(args, i) != 0.0 || args[i].type() != Value::Type::Number;
}

// Argument `i` as a boolean that is `fallback` when omitted.
bool boolArgOr(std::span<const Value> args, std::size_t i, bool fallback) {
    return i >= args.size() ? fallback : boolArg(args, i);
}

// A handle argument: truncated to an unsigned integer.
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(static_cast<std::uint32_t>(wholeArg(args, i)));
}

// A handle argument whose omission (or the original's NilHandle) means none: 0.
double optionalHandleArg(std::span<const Value> args, std::size_t i) {
    if (absent(args, i)) {
        return kNilHandle;
    }
    const double handle = handleArg(args, i);
    return handle == kOmittedHandle ? kNilHandle : handle;
}

// A string argument that is empty for nil.
std::string nameArg(std::span<const Value> args, std::size_t i) {
    return absent(args, i) ? std::string{} : binding::string(args, i);
}

// Numbers 1..3 of the table at argument `i`; 0 for a missing one.
std::array<float, 3> tripleArg(std::span<const Value> args, std::size_t i) {
    std::array<float, 3> values{};
    if (i >= args.size() || args[i].table() == nullptr) {
        return values;
    }
    for (std::size_t k = 0; k < values.size(); ++k) {
        values.at(k) =
            static_cast<float>(args[i].table()->get(Value(static_cast<double>(k + 1))).number().value_or(0.0));
    }
    return values;
}

// The hub host of the level, if there is a level with an AI host.
HubBindingHost* hubOf(const BindingContext& context) { return context.ai != nullptr ? context.ai->hub() : nullptr; }

// The first-mission character host of the level (the flag switches), if any.
HumanBindingHost* humansOf(const BindingContext& context) {
    return context.ai != nullptr ? context.ai->humans() : nullptr;
}

// A binding that hands its arguments to the hub host when there is one and returns nothing. The host is read at each
// call: a level gives its brains to a Lua state made before it (gamemodes/gameplay_mode.h).
template <typename Body> NativeFunction hubCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        if (HubBindingHost* host = hubOf(*context); host != nullptr) {
            body(*host, args);
        }
        return binding::none();
    };
}

// The hub's status of the human `handle` names; nothing without a host or such a human.
std::optional<HubHumanStatus> statusOf(const BindingContext& context, double handle) {
    const HubBindingHost* host = hubOf(context);
    return host != nullptr ? host->status(handle) : std::nullopt;
}

// `HuGetVoiceIndex(human)`: the human's voice set (its type's `CfgChar` voice); -1 (4294967295) for an invalid handle.
// @orig 0x0023adc8 Human_GetVoiceIndex (unknown)
NativeFunction makeHuGetVoiceIndex(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const HumanCreation* made = context->humans != nullptr ? context->humans->find(handleArg(args, 0)) : nullptr;
        if (made == nullptr || context->recorded == nullptr) {
            return binding::number(kNoVoice);
        }
        const int voice = voiceSetOfType(*context->recorded, made->type);
        return binding::number(static_cast<double>(static_cast<std::uint32_t>(voice)));
    };
}

// `HuGiveCuffs(human, count)`: a player's inventory item 5 (with the inventory's callbacks), an AI human's own count.
// @orig 0x002372a8 Human_GiveCuffs (unknown)
NativeFunction makeHuGiveCuffs(ScriptSystem& scripts, const BindingContext& context) {
    return [scripts = &scripts, context = &context](std::span<const Value> args) {
        const double human = handleArg(args, 0);
        // Read as unsigned and added in 32 bits: a negative count takes cuffs away.
        const int count = static_cast<int>(static_cast<std::uint32_t>(wholeArg(args, 1)));
        const std::optional<HubHumanStatus> status = statusOf(*context, human);
        if (!status) {
            return binding::none();
        }
        if (status->playerIndex < 0) {
            if (HubBindingHost* host = hubOf(*context); host != nullptr) {
                host->addCuffs(human, count);
            }
            return binding::none();
        }
        addInventoryItem(*scripts, *context->state, status->playerIndex, item::kHandcuffs, count, true);
        return binding::none();
    };
}

// `HuSetName(human, name)`: the first 15 characters become the name the human is found by.
// @orig 0x00238078 Human_SetName (unknown)
// @orig 0x0021cda8 Human_StoreName (unknown)
NativeFunction makeHuSetName(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        HumanCreation* made = context->humans != nullptr ? context->humans->find(handleArg(args, 0)) : nullptr;
        if (made != nullptr) {
            std::string name = binding::string(args, 1);
            name.resize(std::min(name.size(), kHumanNameLength));
            made->name = std::move(name);
        }
        return binding::none();
    };
}

// `HuAttachGear(human, attach)`: only exactly 1 attaches; each pair needs its upgrade unlocked.
// @orig 0x00236188 Human_SetUnlockedGear (unknown)
NativeFunction makeHuAttachGear(const BindingContext& context) {
    return hubCall(context, [context = &context](HubBindingHost& host, std::span<const Value> args) {
        const bool attach =
            !absent(args, 1) && args[1].type() == Value::Type::Number && binding::number(args, 1) == 1.0;
        const GameState& state = *context->state;
        host.attachGear(handleArg(args, 0), attach,
                        state.player.unlocks.isDataUnlocked(state.saved, kUpgradeType, kKnucklesUpgrade),
                        state.player.unlocks.isDataUnlocked(state.saved, kUpgradeType, kBootsUpgrade));
    });
}

// `HuSetWorkoutCallbacks(onStart, onRep, onEnd)`: the game-wide workout callbacks.
// @orig 0x002345f8 Human_SetWorkoutCallbacks (unknown)
NativeFunction makeHuSetWorkoutCallbacks(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        WorkoutSettings& workout = context->state->hub.workout;
        workout.onStart = nameArg(args, 0);
        workout.onRep = nameArg(args, 1);
        workout.onEnd = nameArg(args, 2);
        return binding::none();
    };
}

// `CfgWorkoutParams(rates1, rates2, rates3, factor)`: the three tables, and the factor read as a boolean (so 0 or 1).
// @orig 0x00234530 Cfg_SetWorkoutParams (unknown)
NativeFunction makeCfgWorkoutParams(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        WorkoutSettings& workout = context->state->hub.workout;
        for (std::size_t table = 0; table < workout.rates.size(); ++table) {
            workout.rates.at(table) = tripleArg(args, table);
        }
        workout.factor = boolArg(args, 3) ? 1.0F : 0.0F;
        return binding::none();
    };
}

// ---- The gangs and the objects ----

// `GangClearHandlers(gang)`: the gang's own handlers, then every slot of each member's.
// @orig 0x0016ab90 Gang_ClearHandlers (unknown)
// @orig 0x003858b8 ScriptHandlers_ClearAll (unknown)
NativeFunction makeGangClearHandlers(const BindingContext& context) {
    return hubCall(context, [context = &context](HubBindingHost& host, std::span<const Value> args) {
        const int gang = static_cast<std::int16_t>(intArg(args, 0));
        if (gang < 0) {
            return;
        }
        host.clearGangHandlers(gang);
        if (context->messages == nullptr) {
            return;
        }
        for (const double member : host.memberHandles(gang)) {
            for (int message = 0; message < MessageHandlers::kMessages; ++message) {
                context->messages->set(member, message, {});
            }
        }
    });
}

// `ObjIsAlive(object)`: whether the handle names a live object: a human, a flag, a car, a particle system or a spawn
// record that is not removed (resolving a record spawns its object).
// @orig 0x00397598 Obj_Exists (unknown)
NativeFunction makeObjIsAlive(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const double handle = handleArg(args, 0);
        if (handle == kNilHandle || handle == kOmittedHandle) {
            return binding::boolean(false);
        }
        const HubBindingHost* host = hubOf(*context);
        const bool alive = (host != nullptr && host->alive(handle)) ||
                           (context->flags != nullptr && context->flags->find(handle) != nullptr) ||
                           (context->cars != nullptr && context->cars->find(handle) != nullptr) ||
                           (context->effects != nullptr && context->effects->particles.find(handle) != nullptr) ||
                           (context->spawnRecords != nullptr && context->spawnRecords->resolve(handle) != nullptr);
        return binding::boolean(alive);
    };
}

// `GetObjectName(object)`: a world object's type name (resolving a spawn record spawns it); `<null>` for anything else.
// @orig 0x003859f0 Obj_GetTypeName (unknown)
NativeFunction makeGetObjectName(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) -> binding::Results {
        const world_objects::SpawnRecord* record =
            context->spawnRecords != nullptr ? context->spawnRecords->resolve(handleArg(args, 0)) : nullptr;
        return std::vector<Value>{Value(record != nullptr ? record->typeName : std::string(kNullName))};
    };
}

} // namespace

void addHubBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context) {
    using A = std::span<const Value>;
    // ---- The humans: getters.
    // @orig 0x00238158 Human_GetMoney (unknown)
    vm.registerFunction("HuGetMoney", [context = &context](A args) {
        const std::optional<HubHumanStatus> status = statusOf(*context, handleArg(args, 0));
        return binding::number(status ? status->money : 0);
    });
    // `HuIsDead(human)`: dead, or the handle no longer resolves.
    // @orig 0x00235688 Human_IsDead (unknown)
    vm.registerFunction("HuIsDead", [context = &context](A args) {
        const std::optional<HubHumanStatus> status = statusOf(*context, handleArg(args, 0));
        return binding::boolean(!status || status->dead);
    });
    vm.registerFunction("HuGetVoiceIndex", makeHuGetVoiceIndex(context));

    // ---- The humans: switches and set-up.
    vm.registerFunction("HuAttachGear", makeHuAttachGear(context));
    vm.registerFunction("HuGiveCuffs", makeHuGiveCuffs(scripts, context));
    // @orig 0x00237b70 Human_SetBlockTackle (unknown)
    vm.registerFunction("HuBlockTackle", [context = &context](A args) {
        if (HumanBindingHost* host = humansOf(*context); host != nullptr) {
            host->setFlags(handleArg(args, 0), human::flag::kBlockTackle, boolArg(args, 1));
        }
        return binding::none();
    });
    vm.registerFunction("HuSetScale",
                        hubCall(context, [](HubBindingHost& h, A a) { h.setScale(handleArg(a, 0), floatArg(a, 1)); }));
    vm.registerFunction("HuSetName", makeHuSetName(context));
    vm.registerFunction(
        "HuSetMug", hubCall(context, [](HubBindingHost& h, A a) { h.setMuggable(handleArg(a, 0), boolArg(a, 1)); }));
    // The reaction is read as 16 bits and sign-extended; 7 is kept as it is.
    vm.registerFunction("HuSetPedReaction", hubCall(context, [](HubBindingHost& h, A a) {
                            h.setPedReaction(handleArg(a, 0), static_cast<std::int16_t>(intArg(a, 1)));
                        }));
    vm.registerFunction("HuSetUnarrestable", hubCall(context, [](HubBindingHost& h, A a) {
                            h.setUnarrestable(handleArg(a, 0), boolArg(a, 1));
                        }));
    vm.registerFunction("HuSetCombatMode", hubCall(context, [](HubBindingHost& h, A a) {
                            h.setCombatMode(handleArg(a, 0), boolArg(a, 1));
                        }));
    // `HuSetLookAtTarget(human, target)`: does nothing in this build (its callee returns at once).
    // @orig 0x00299238 Human_SetLookAtTarget (unknown)
    vm.registerFunction("HuSetLookAtTarget", [](A) { return binding::none(); });
    vm.registerFunction("HuPlayDynAnim", hubCall(context, [](HubBindingHost& h, A a) {
                            h.playDynAnim(handleArg(a, 0), binding::string(a, 1));
                        }));

    // ---- The workouts.
    vm.registerFunction("HuWorkout", hubCall(context, [](HubBindingHost& h, A a) {
                            WorkoutCall call{.human = handleArg(a, 0), .equipment = handleArg(a, 1), .clips = {}};
                            for (std::size_t clip = 0; clip < call.clips.size(); ++clip) {
                                call.clips.at(clip) = binding::string(a, clip + 2);
                            }
                            h.workout(call);
                        }));
    vm.registerFunction("HuStopWorkout",
                        hubCall(context, [](HubBindingHost& h, A a) { h.stopWorkout(handleArg(a, 0)); }));
    vm.registerFunction("HuSetWorkoutBlend", hubCall(context, [](HubBindingHost& h, A a) {
                            h.setWorkoutBlend(handleArg(a, 0), floatArg(a, 1));
                        }));
    vm.registerFunction("HuSetWorkoutCallbacks", makeHuSetWorkoutCallbacks(context));
    vm.registerFunction("CfgWorkoutParams", makeCfgWorkoutParams(context));

    // ---- The brains.
    vm.registerFunction("BrCanUseWorldFlags", hubCall(context, [](HubBindingHost& h, A a) {
                            h.canUseWorldFlags(handleArg(a, 0), boolArgOr(a, 1, true),
                                               a.size() > 2
                                                   ? static_cast<int>(static_cast<std::uint32_t>(wholeArg(a, 2)))
                                                   : kDefaultWorldFlagChance);
                        }));
    vm.registerFunction("BrHasEnemies", [context = &context](A args) {
        const HubBindingHost* host = hubOf(*context);
        return binding::boolean(host != nullptr && host->hasEnemies(handleArg(args, 0)));
    });

    // ---- The goals.
    vm.registerFunction("GoalAreaWalker", hubCall(context, [](HubBindingHost& h, A a) {
                            h.goalAreaWalker(
                                AreaWalkerCall{.human = handleArg(a, 0),
                                               .flag = optionalHandleArg(a, 1),
                                               .radius = intArg(a, 2),
                                               .mode = intArg(a, 3),
                                               .durationSeconds = static_cast<std::uint32_t>(wholeArg(a, 4)),
                                               .pauseSeconds = intArg(a, 5)});
                        }));
    vm.registerFunction(
        "GoalBoxer", hubCall(context, [](HubBindingHost& h, A a) { h.goalBoxer(handleArg(a, 0), handleArg(a, 1)); }));
    vm.registerFunction("GoalGrabTarget", hubCall(context, [](HubBindingHost& h, A a) {
                            h.goalGrabTarget(handleArg(a, 0), handleArg(a, 1));
                        }));
    vm.registerFunction("GoalPeddler", hubCall(context, [](HubBindingHost& h, A a) {
                            h.goalPeddler(PeddlerCall{.human = handleArg(a, 0),
                                                      .range = floatArg(a, 1),
                                                      .reacts = boolArg(a, 2),
                                                      .greetAnim = nameArg(a, 3),
                                                      .idleAnim = nameArg(a, 4)});
                        }));
    vm.registerFunction("GoalPlayGenAnim", hubCall(context, [](HubBindingHost& h, A a) {
                            h.goalPlayGenAnim(handleArg(a, 0),
                                              static_cast<int>(static_cast<std::uint32_t>(wholeArg(a, 1))),
                                              nameArg(a, 2));
                        }));
    vm.registerFunction("GoalShopkeeper", hubCall(context, [](HubBindingHost& h, A a) {
                            h.goalShopkeeper(ShopkeeperCall{.human = handleArg(a, 0),
                                                            .store = handleArg(a, 1),
                                                            .kind = intArg(a, 2),
                                                            .broom = boolArg(a, 3),
                                                            .range = a.size() > 4 ? floatArg(a, 4) : kDefaultShopRange,
                                                            .onDisturbed = nameArg(a, 5),
                                                            .onPhone = nameArg(a, 6),
                                                            .pleads = boolArgOr(a, 7, true)});
                        }));

    // ---- The gangs: an id is read as 16 bits; -1 is no gang.
    vm.registerFunction("GangCanFlee", hubCall(context, [](HubBindingHost& h, A a) {
                            h.canFlee(static_cast<std::int16_t>(intArg(a, 0)), boolArg(a, 1));
                        }));
    vm.registerFunction("GangClearBums", hubCall(context, [](HubBindingHost& h, A) { h.clearBums(); }));
    vm.registerFunction("GangClearHandlers", makeGangClearHandlers(context));
    // `GangGoodToGo(gang, ignoreBusy)`: nil for gang -1.
    vm.registerFunction("GangGoodToGo", [context = &context](A args) {
        const int gang = intArg(args, 0);
        const HubBindingHost* host = hubOf(*context);
        return binding::boolean(gang != -1 && host != nullptr && host->goodToGo(gang, boolArg(args, 1)));
    });
    vm.registerFunction("GangIsASpawner", [context = &context](A args) {
        const int gang = intArg(args, 0);
        const HubBindingHost* host = hubOf(*context);
        return binding::boolean(gang != -1 && host != nullptr && host->isASpawner(gang, binding::string(args, 1)));
    });
    vm.registerFunction("GangMakeNeutralOfType", hubCall(context, [](HubBindingHost& h, A a) {
                            const int gang = intArg(a, 0);
                            if (gang != -1) {
                                h.makeNeutralOfType(gang, intArg(a, 1));
                            }
                        }));

    // ---- The objects.
    vm.registerFunction("ObjIsAlive", makeObjIsAlive(context));
    vm.registerFunction("GetObjectName", makeGetObjectName(context));
}

} // namespace coney::script
