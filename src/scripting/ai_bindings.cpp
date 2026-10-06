// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/ai_bindings.h"

#include <cmath>
#include <cstddef>
#include <span>

#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as a boolean: nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].isNil()) {
        return false;
    }
    return binding::number(args, i) != 0.0 || args[i].type() != Value::Type::Number;
}

// A handle argument: truncated to an unsigned integer.
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(static_cast<std::uint32_t>(wholeArg(args, i)));
}

// `GoalMoveToFlag(human, flag, gait, angle, distance, radius, intervalMs, faceFlag, option)`.
// @orig 0x0035ff18 GoalMoveToFlag (unknown)
NativeFunction makeGoalMoveToFlag(const BindingContext& context) {
    return [host = context.ai](std::span<const Value> args) {
        if (host != nullptr) {
            host->goalMoveToFlag(MoveToFlagCall{.human = handleArg(args, 0),
                                                .flag = handleArg(args, 1),
                                                .gait = static_cast<int>(wholeArg(args, 2)),
                                                .angle = static_cast<float>(binding::number(args, 3)),
                                                .distance = static_cast<float>(binding::number(args, 4)),
                                                .radius = static_cast<float>(binding::number(args, 5)),
                                                .intervalMs = static_cast<std::uint32_t>(wholeArg(args, 6)),
                                                .faceFlag = boolArg(args, 7),
                                                .option = boolArg(args, 8)});
        }
        return binding::none();
    };
}

// `ActLookAt(human, target, turnSpeed, timeMs)`: the fourth argument defaults to -1.
// @orig 0x003647d8 ActLookAt (unknown)
NativeFunction makeActLookAt(const BindingContext& context) {
    return [host = context.ai](std::span<const Value> args) {
        if (host != nullptr) {
            const bool delayGiven = args.size() > 3 && !args[3].isNil();
            host->actLookAt(LookAtCall{.human = handleArg(args, 0),
                                       .target = handleArg(args, 1),
                                       .turn = static_cast<float>(binding::number(args, 2)),
                                       .delayMs = static_cast<std::int16_t>(delayGiven ? wholeArg(args, 3) : -1)});
        }
        return binding::none();
    };
}

} // namespace

void addAiBindings(LuaVm& vm, const BindingContext& context) {
    vm.registerFunction("GoalMoveToFlag", makeGoalMoveToFlag(context));
    vm.registerFunction("ActLookAt", makeActLookAt(context));
}

} // namespace coney::script
