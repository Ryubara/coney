// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/effects_bindings.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

#include "effects/level_effects.h"
#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// The value of the global `NilHandle`: what `SpawnParticle` returns when the pool is full.
constexpr double kNilHandle = 0.0;

// Argument `i` as tolua reads an unsigned integer: truncated, then kept to 32 bits (a negative number wraps).
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(std::trunc(binding::number(args, i))));
}

// Element `k` (1-based) of the table argument `i` as a number; 0 when missing, as tolua reads it.
double tableNumber(std::span<const Value> args, std::size_t i, std::size_t k) {
    if (i >= args.size() || args[i].table() == nullptr) {
        return 0.0;
    }
    return args[i].table()->get(Value(static_cast<double>(k))).number().value_or(0.0);
}

// `SpawnParticle(typeName, pos, rot, attachTo)`: a particle system and its handle.
// @orig 0x00378958 SpawnParticle (unknown)
NativeFunction makeSpawnParticle(const BindingContext& context, std::function<double()> nextHandle) {
    // The context's effects are read at each call: gameplay sets them when a level is entered, after the bindings.
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        effects::LevelEffects* effects = context->effects;
        const double handle = nextHandle();
        if (effects == nullptr) {
            return binding::number(handle);
        }
        // tolua reads a missing coordinate as 0, and the rotation's four as they are.
        const std::array<float, 3> p = binding::position(args, 1).value_or(std::array<float, 3>{});
        const anim::Quat rotation{
            static_cast<float>(tableNumber(args, 2, 1)), static_cast<float>(tableNumber(args, 2, 2)),
            static_cast<float>(tableNumber(args, 2, 3)), static_cast<float>(tableNumber(args, 2, 4))};
        const effects::ParticleSystem* system = effects->particles.spawn(
            binding::string(args, 0), anim::Vec3{p[0], p[1], p[2]}, rotation, unsignedArg(args, 3), handle);
        return binding::number(system != nullptr ? handle : kNilHandle);
    };
}

// `QueueMotionBlurEffect(alpha, seconds)` or `QueueMotionBlurEffect({r, g, b, a}, seconds)`: the overload is chosen by
// the first argument's type, as the wrapper checks it.
// @orig 0x0037be90 QueueMotionBlurEffect (unknown)
// @orig 0x0037be18 QueueMotionBlurEffect_Alpha (unknown)
NativeFunction makeQueueMotionBlurEffect(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        effects::LevelEffects* effects = context->effects;
        if (effects == nullptr) {
            return binding::none();
        }
        const auto seconds = static_cast<float>(binding::number(args, 1));
        if (!args.empty() && args[0].table() != nullptr) {
            // **Coney's choice**: each component is kept to its low byte, as the strength is.
            const auto component = [&args](std::size_t k) {
                return static_cast<std::uint8_t>(static_cast<std::int64_t>(std::trunc(tableNumber(args, 0, k))));
            };
            effects->motionBlur.queueColour({component(1), component(2), component(3), component(4)}, seconds);
        } else {
            effects->motionBlur.queueAlpha(static_cast<std::uint8_t>(unsignedArg(args, 0)), seconds);
        }
        return binding::none();
    };
}

// A table argument `{r, g, b, a}` (0-1 each) as the screen tint stores it: each × 255, truncated.
effects::ScreenTint::Colour tintArg(std::span<const Value> args) {
    const auto component = [&args](std::size_t k) { return effects::ScreenTint::byteOf(tableNumber(args, 0, k)); };
    return effects::ScreenTint::Colour{component(1), component(2), component(3), component(4)};
}

// The tint the tint bindings set: the context's own (the front end's), else the level's effects', else none.
effects::ScreenTint* tintOf(const BindingContext& context) {
    if (context.tint != nullptr) {
        return context.tint;
    }
    return context.effects != nullptr ? &context.effects->tint : nullptr;
}

// `SetLevelColour({r, g, b, a})`, `EnterStore({r, g, b, a})` and `ExitStore()`: the screen tint's looks 9 and 10
// (docs/research/rendering.md#tint).
void addTintBindings(LuaVm& vm, const BindingContext& context) {
    vm.registerFunction("SetLevelColour", [context = &context](std::span<const Value> args) {
        if (effects::ScreenTint* tint = tintOf(*context)) {
            tint->setLevelColour(tintArg(args));
        }
        return binding::none();
    });
    vm.registerFunction("EnterStore", [context = &context](std::span<const Value> args) {
        if (effects::ScreenTint* tint = tintOf(*context)) {
            tint->enterStore(tintArg(args));
        }
        return binding::none();
    });
    vm.registerFunction("ExitStore", [context = &context](std::span<const Value>) {
        if (effects::ScreenTint* tint = tintOf(*context)) {
            tint->exitStore();
        }
        return binding::none();
    });
}

} // namespace

void addEffectsBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle) {
    vm.registerFunction("QueueMotionBlurEffect", makeQueueMotionBlurEffect(context));
    vm.registerFunction("SpawnParticle", makeSpawnParticle(context, std::move(nextHandle)));
    addTintBindings(vm, context);
}

} // namespace coney::script
