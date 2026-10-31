// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/shadow_bindings.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "graphics/level_lighting.h"
#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// Argument `i` as a single-precision number: 0 when missing, as the wrappers' optional numbers.
// @coney-only tolua's argument reading, which the original's generated wrappers do
float floatArg(std::span<const Value> args, std::size_t i) { return static_cast<float>(binding::number(args, i)); }

// Argument `i` as an object handle: truncated and kept to 0 .. 2^32 - 1, as `EnableShadow`'s wrapper (0x0037c748)
// converts it with the soft-float library's Float64_ToUInt32Saturating (0x0042c420, middleware, so no @orig;
// docs/research/scripting.md#argument-and-result-conventions).
// @coney-only tolua's argument reading, which the original's generated wrappers do
std::uint32_t handleArg(std::span<const Value> args, std::size_t i) {
    const double value = std::trunc(binding::number(args, i));
    if (!(value > 0.0)) {
        return 0;
    }
    constexpr double kLargest = 4294967295.0;
    return value >= kLargest ? 0xffffffffU : static_cast<std::uint32_t>(value);
}

// Argument `i` as the wrappers' optional boolean: false when missing or nil, else whether its number is not 0, as
// tolua's Lua_GetOptionalBoolArg (0x00409318, middleware, so no @orig;
// docs/research/scripting.md#argument-and-result-conventions) reads it.
// @coney-only tolua's argument reading, which the original's generated wrappers do
bool optionalBooleanArg(std::span<const Value> args, std::size_t i) {
    return i < args.size() && !args[i].isNil() && binding::number(args, i) != 0.0;
}

} // namespace

void addShadowBindings(LuaVm& vm, const BindingContext& context) {
    const BindingContext* ctx = &context;

    // `SetShadowColor(r, g, b)`: the level's shadow colour.
    // @orig 0x0017f260 Shadow_SetColour_Stub (unknown)
    vm.registerFunction("SetShadowColor", [ctx](std::span<const Value> args) -> binding::Results {
        graphics::LevelLighting* lighting = ctx->lighting;
        if (lighting != nullptr) {
            lighting->shadow.setColour(floatArg(args, 0), floatArg(args, 1), floatArg(args, 2));
        }
        return binding::none();
    });

    // `SetShadowLightOffset(x, y, z)`: the level's shadow light offset, in the game's axes.
    // @orig 0x0017f268 Shadow_SetLightOffset_Stub (unknown)
    vm.registerFunction("SetShadowLightOffset", [ctx](std::span<const Value> args) -> binding::Results {
        graphics::LevelLighting* lighting = ctx->lighting;
        if (lighting != nullptr) {
            lighting->shadow.setOffset(floatArg(args, 0), floatArg(args, 1), floatArg(args, 2));
        }
        return binding::none();
    });

    // `EnableShadow(object, enable)`: whether the object casts a shadow; `enable` is false when nil or missing.
    // @orig 0x0017f270 Shadow_Enable_Stub (unknown)
    vm.registerFunction("EnableShadow", [ctx](std::span<const Value> args) -> binding::Results {
        graphics::LevelLighting* lighting = ctx->lighting;
        if (lighting != nullptr) {
            lighting->casters.setEnabled(handleArg(args, 0), optionalBooleanArg(args, 1));
        }
        return binding::none();
    });
}

} // namespace coney::script
