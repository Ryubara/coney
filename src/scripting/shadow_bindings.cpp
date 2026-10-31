// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/shadow_bindings.h"

#include <span>

#include "graphics/level_lighting.h"
#include "scripting/binding_args.h"

namespace coney::script {

// The arguments are read as the wrappers read them, with the shared readers of binding_args.h: three optional
// floats (0x0037c608, 0x0037c6a8), and for `EnableShadow` (0x0037c748) an unsigned handle (unsignedArg(), the
// soft-float conversion at 0x0042c420) and an optional boolean (booleanArg(), tolua's reader at 0x00409318).
void addShadowBindings(LuaVm& vm, const BindingContext& context) {
    const BindingContext* ctx = &context;
    using binding::booleanArg;
    using binding::floatArg;
    using binding::unsignedArg;

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
            lighting->casters.setEnabled(unsignedArg(args, 0), booleanArg(args, 1));
        }
        return binding::none();
    });
}

} // namespace coney::script
