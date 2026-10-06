// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/ai_bindings.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as an integer.
int intArg(std::span<const Value> args, std::size_t i) { return static_cast<int>(wholeArg(args, i)); }

// Argument `i` as a float.
float floatArg(std::span<const Value> args, std::size_t i) { return static_cast<float>(binding::number(args, i)); }

// Whether argument `i` is missing or nil, so a default applies.
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
    return absent(args, i) ? fallback : boolArg(args, i);
}

// A handle argument: truncated to an unsigned integer.
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(static_cast<std::uint32_t>(wholeArg(args, i)));
}

// A string argument that is empty for nil (a callback name).
std::string nameArg(std::span<const Value> args, std::size_t i) {
    return absent(args, i) ? std::string{} : binding::string(args, i);
}

// A binding that hands its arguments to the host when there is one and returns nothing. The host is read at each call,
// not at registration: a level gives its brains to a Lua state made before it (gamemodes/gameplay_mode.h).
template <typename Body> NativeFunction hostCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        if (AiBindingHost* host = context->ai; host != nullptr) {
            body(*host, args);
        }
        return binding::none();
    };
}

// `GoalMoveToFlag(human, flag, gait, angle, distance, radius, intervalMs, faceFlag, option)`.
// @orig 0x0035ff18 GoalMoveToFlag (unknown)
NativeFunction makeGoalMoveToFlag(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.goalMoveToFlag(MoveToFlagCall{.human = handleArg(args, 0),
                                           .flag = handleArg(args, 1),
                                           .gait = intArg(args, 2),
                                           .angle = floatArg(args, 3),
                                           .distance = floatArg(args, 4),
                                           .radius = floatArg(args, 5),
                                           .intervalMs = static_cast<std::uint32_t>(wholeArg(args, 6)),
                                           .faceFlag = boolArg(args, 7),
                                           .option = boolArg(args, 8)});
    });
}

// `ActLookAt(human, target, turnSpeed, timeMs)`: the fourth argument defaults to -1.
// @orig 0x003647d8 ActLookAt (unknown)
NativeFunction makeActLookAt(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.actLookAt(LookAtCall{.human = handleArg(args, 0),
                                  .target = handleArg(args, 1),
                                  .turn = floatArg(args, 2),
                                  .delayMs = static_cast<std::int16_t>(absent(args, 3) ? -1 : wholeArg(args, 3))});
    });
}

// `GoalFight(human, target, unused)`: the third argument is read and never used.
// @orig 0x003610a8 GoalFight (unknown)
NativeFunction makeGoalFight(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.goalFight(handleArg(args, 0), handleArg(args, 1));
    });
}

// `BrFlush(human)`.
// @orig 0x0035ede0 BrFlush (unknown)
NativeFunction makeBrFlush(const BindingContext& context) {
    return hostCall(context,
                    [](AiBindingHost& host, std::span<const Value> args) { host.brFlush(handleArg(args, 0)); });
}

// `BrDead(human, dead)`: dead defaults to true.
// @orig 0x0035eb78 BrDead (unknown)
NativeFunction makeBrDead(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.brDead(handleArg(args, 0), boolArgOr(args, 1, true));
    });
}

// `BrSuspend(human, suspended)`: suspended defaults to true.
// @orig 0x0035ebd8 BrSuspend (unknown)
NativeFunction makeBrSuspend(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.brSuspend(handleArg(args, 0), boolArgOr(args, 1, true));
    });
}

// `BrSetThreatResponse(human, response)`.
// @orig 0x0035ef48 BrSetThreatResponse (unknown)
NativeFunction makeBrSetThreatResponse(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.brSetThreatResponse(handleArg(args, 0), intArg(args, 1));
    });
}

// `GoalPlayDynAnimation(human, anim, callback, option)`: option defaults to true.
// @orig 0x00363530 GoalPlayDynAnimation (unknown)
NativeFunction makeGoalPlayDynAnimation(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.goalPlayDynAnimation(DynAnimationCall{.human = handleArg(args, 0),
                                                   .anim = nameArg(args, 1),
                                                   .callback = nameArg(args, 2),
                                                   .option = boolArgOr(args, 3, true)});
    });
}

// `GoalAddressPerson(human, target, approach, range, speech, callback)`: speech defaults to -1.
// @orig 0x003603a8 GoalAddressPerson (unknown)
NativeFunction makeGoalAddressPerson(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.goalAddressPerson(AddressPersonCall{.human = handleArg(args, 0),
                                                 .target = handleArg(args, 1),
                                                 .approach = floatArg(args, 2),
                                                 .range = floatArg(args, 3),
                                                 .speech = absent(args, 4) ? -1 : intArg(args, 4),
                                                 .callback = nameArg(args, 5)});
    });
}

// `GoalTrackHuman(human, target, distance)`.
// @orig 0x00360690 GoalTrackHuman (unknown)
NativeFunction makeGoalTrackHuman(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.goalTrackHuman(handleArg(args, 0), handleArg(args, 1), floatArg(args, 2));
    });
}

// `GoalDealer(human, dealerType, range, runChance, dirtyChance, option)`: range 10, run chance 50 and option true
// when omitted.
// @orig 0x00363018 GoalDealer (unknown)
NativeFunction makeGoalDealer(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.goalDealer(DealerCall{.human = handleArg(args, 0),
                                   .type = intArg(args, 1),
                                   .range = absent(args, 2) ? 10.0F : floatArg(args, 2),
                                   .runChance = absent(args, 3) ? 50 : intArg(args, 3),
                                   .dirtyChance = intArg(args, 4),
                                   .option = boolArgOr(args, 5, true)});
    });
}

// `BrSetNumFollowSlots(leader, count, count2)`: count2 defaults to -1.
// @orig 0x0035fc80 BrSetNumFollowSlots (unknown)
NativeFunction makeBrSetNumFollowSlots(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.brSetNumFollowSlots(handleArg(args, 0), intArg(args, 1), absent(args, 2) ? -1 : intArg(args, 2));
    });
}

// `BrSetFollowSlot(leader, slot, {x, y}, set)`. **Coney choice**: the offset is not written back into the table (the
// original writes back the fixed-point values, which Coney does not need).
// @orig 0x0035fd90 BrSetFollowSlot (unknown)
NativeFunction makeBrSetFollowSlot(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        float x = 0.0F;
        float y = 0.0F;
        if (args.size() > 2 && args[2].table() != nullptr) {
            const Table& offset = *args[2].table();
            x = static_cast<float>(offset.get(Value(1.0)).number().value_or(0.0));
            y = static_cast<float>(offset.get(Value(2.0)).number().value_or(0.0));
        }
        host.brSetFollowSlot(handleArg(args, 0), intArg(args, 1), x, y, intArg(args, 3));
    });
}

// `BrSetFollowSlotSet(leader, set)`.
// @orig 0x0035fd18 BrSetFollowSlotSet (unknown)
NativeFunction makeBrSetFollowSlotSet(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.brSetFollowSlotSet(handleArg(args, 0), intArg(args, 1));
    });
}

// `TacticCrowd(gang, callback, option)`.
// @orig 0x00375648 TacticCrowd (unknown)
NativeFunction makeTacticCrowd(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.tacticCrowd(intArg(args, 0), nameArg(args, 1), boolArg(args, 2));
    });
}

// `TacticTrigger(gang, what, enabled)`: enabled is read as a number (the scripts' true is 1).
// @orig 0x00377a10 TacticTrigger (unknown)
NativeFunction makeTacticTrigger(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.tacticTrigger(intArg(args, 0), intArg(args, 1), wholeArg(args, 2) != 0);
    });
}

// `TacticClear(gang)`.
// @orig 0x00374a80 TacticClear (unknown)
NativeFunction makeTacticClear(const BindingContext& context) {
    return hostCall(context,
                    [](AiBindingHost& host, std::span<const Value> args) { host.tacticClear(intArg(args, 0)); });
}

} // namespace

void addAiBindings(LuaVm& vm, const BindingContext& context) {
    vm.registerFunction("GoalMoveToFlag", makeGoalMoveToFlag(context));
    vm.registerFunction("ActLookAt", makeActLookAt(context));
    vm.registerFunction("GoalFight", makeGoalFight(context));
    vm.registerFunction("BrFlush", makeBrFlush(context));
    vm.registerFunction("BrDead", makeBrDead(context));
    vm.registerFunction("BrSuspend", makeBrSuspend(context));
    vm.registerFunction("BrSetThreatResponse", makeBrSetThreatResponse(context));
    vm.registerFunction("GoalPlayDynAnimation", makeGoalPlayDynAnimation(context));
    vm.registerFunction("GoalAddressPerson", makeGoalAddressPerson(context));
    vm.registerFunction("GoalTrackHuman", makeGoalTrackHuman(context));
    vm.registerFunction("GoalDealer", makeGoalDealer(context));
    vm.registerFunction("BrSetNumFollowSlots", makeBrSetNumFollowSlots(context));
    vm.registerFunction("BrSetFollowSlot", makeBrSetFollowSlot(context));
    vm.registerFunction("BrSetFollowSlotSet", makeBrSetFollowSlotSet(context));
    vm.registerFunction("TacticCrowd", makeTacticCrowd(context));
    vm.registerFunction("TacticTrigger", makeTacticTrigger(context));
    vm.registerFunction("TacticClear", makeTacticClear(context));
}

} // namespace coney::script
