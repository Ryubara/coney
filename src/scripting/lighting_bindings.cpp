// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/lighting_bindings.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "graphics/level_lighting.h"
#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// Argument `i` as tolua reads an unsigned integer: truncated, then kept to 32 bits.
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(std::trunc(binding::number(args, i))));
}

// Argument `i` as a single-precision number.
float floatArg(std::span<const Value> args, std::size_t i) { return static_cast<float>(binding::number(args, i)); }

// The first `N` numbers of the table argument `i` (t[1]..t[N]); a missing element reads 0, a non-table nothing.
template <std::size_t N> std::optional<std::array<float, N>> tableArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].table() == nullptr) {
        return std::nullopt;
    }
    std::array<float, N> values{};
    for (std::size_t k = 0; k < N; ++k) {
        const std::optional<double> value = args[i].table()->get(Value(static_cast<double>(k + 1))).number();
        values.at(k) = static_cast<float>(value.value_or(0.0));
    }
    return values;
}

// The states `SetLight` takes: off, on, remove.
constexpr std::uint32_t kStateOff = 0;
constexpr std::uint32_t kStateRemove = 2;
// The largest type, lights and effects values `SetLight` accepts, and its corona's.
constexpr std::uint32_t kMaxType = 3;
constexpr std::uint32_t kMaxLights = 3;
constexpr std::uint32_t kMaxEffects = 32;
constexpr std::uint32_t kMaxCorona = 6;

// `SetLight`'s long form arguments as a descriptor; nothing when they are refused.
std::optional<graphics::LightDescriptor> descriptorOf(std::span<const Value> args) {
    const std::uint32_t type = unsignedArg(args, 1);
    const float radius = floatArg(args, 5);
    const float cone = floatArg(args, 6);
    const float pull = floatArg(args, 8);
    const float size = floatArg(args, 9);
    const std::uint32_t lights = unsignedArg(args, 10);
    const std::uint32_t effects = unsignedArg(args, 11);
    const std::uint32_t corona = unsignedArg(args, 12);
    if (type > kMaxType || radius < 0.0F || cone < 0.0F || lights > kMaxLights || effects > kMaxEffects ||
        corona > kMaxCorona || (corona != 0 && (pull < 0.0F || size < 0.0F))) {
        return std::nullopt;
    }
    static constexpr std::array kTypes{graphics::LightType::Point, graphics::LightType::Spot,
                                       graphics::LightType::Directional, graphics::LightType::Ambient};
    graphics::LightDescriptor desc;
    desc.type = kTypes.at(type);
    // Positions and directions from the game's axes into RenderWare's.
    if (const auto pos = tableArg<3>(args, 2)) {
        desc.position = graphics::gameToRenderWare((*pos)[0], (*pos)[1], (*pos)[2]);
    }
    if (const auto dir = tableArg<3>(args, 3)) {
        desc.direction = graphics::gameToRenderWare((*dir)[0], (*dir)[1], (*dir)[2]);
    }
    if (const auto colour = tableArg<4>(args, 4)) {
        desc.colour = graphics::LightColour{(*colour)[0], (*colour)[1], (*colour)[2], (*colour)[3]};
    }
    desc.radius = radius;
    desc.coneAngle = cone;
    desc.coronaHeight = floatArg(args, 7);
    desc.coronaPull = pull;
    desc.coronaSize = size;
    desc.lights = static_cast<std::uint16_t>(lights);
    desc.effects = effects;
    desc.corona = static_cast<std::int16_t>(static_cast<int>(corona) - 1);
    desc.on = unsignedArg(args, 13) != kStateOff;
    return desc;
}

// `SetLight(light, state)`: switches a light off (0) or on (1), or removes it (2); returns the handle, or 0 when it was
// removed or there was none.
// @orig 0x0017f160 Light_SetState (LightManager.cpp)
binding::Results setLightState(graphics::LightManager& lights, std::span<const Value> args) {
    const graphics::LightHandle handle = unsignedArg(args, 0);
    const std::uint32_t state = unsignedArg(args, 1);
    if (handle == 0 || lights.light(handle) == nullptr) {
        return binding::number(0.0);
    }
    if (state == kStateRemove) {
        lights.removeLight(handle);
        return binding::number(0.0);
    }
    lights.setOn(handle, state != kStateOff);
    return binding::number(static_cast<double>(handle));
}

// `SetLight`'s long form: makes a light (handle 0) or changes one, or removes it (state 2); returns the handle, or 0
// when refused or removed.
// @orig 0x0017ef20 Light_SetFromScript (LightManager.cpp)
binding::Results setLightLong(graphics::LightManager& lights, std::span<const Value> args) {
    const graphics::LightHandle handle = unsignedArg(args, 0);
    if (handle != 0 && unsignedArg(args, 13) == kStateRemove) {
        lights.removeLight(handle);
        return binding::number(0.0);
    }
    const std::optional<graphics::LightDescriptor> desc = descriptorOf(args);
    if (!desc) {
        return binding::number(0.0);
    }
    if (handle == 0) {
        return binding::number(static_cast<double>(lights.addLight(*desc)));
    }
    return binding::number(lights.setLight(handle, *desc) ? static_cast<double>(handle) : 0.0);
}

// Whether `SetLight` was called in its short form: exactly two numbers.
bool isShortForm(std::span<const Value> args) {
    return args.size() == 2 && args[0].number().has_value() && args[1].number().has_value();
}

} // namespace

void addLightingBindings(LuaVm& vm, const BindingContext& context) {
    // The context, not its lighting: gameplay gives each level a fresh one after the bindings are registered.
    const BindingContext* ctx = &context;

    vm.registerFunction("SetLight", [ctx](std::span<const Value> args) -> binding::Results {
        graphics::LevelLighting* lighting = ctx->lighting;
        if (lighting == nullptr) {
            return binding::number(0.0);
        }
        return isShortForm(args) ? setLightState(lighting->lights, args) : setLightLong(lighting->lights, args);
    });

    // `SetLightFlicker(light, p1 ... p10)`; does nothing for light 0.
    // @orig 0x0017f1b8 Light_SetFlicker (LightManager.cpp)
    vm.registerFunction("SetLightFlicker", [ctx](std::span<const Value> args) -> binding::Results {
        graphics::LevelLighting* lighting = ctx->lighting;
        if (lighting != nullptr) {
            const graphics::FlickerTiming timing{
                .onTime = unsignedArg(args, 1),
                .onRandom = unsignedArg(args, 2),
                .offTime = unsignedArg(args, 3),
                .offRandom = unsignedArg(args, 4),
                .pause = unsignedArg(args, 5),
                .pauseRandom = unsignedArg(args, 6),
                .burst = unsignedArg(args, 7),
                .burstRandom = unsignedArg(args, 8),
                .flickerTime = unsignedArg(args, 9),
                .dim = unsignedArg(args, 10),
            };
            lighting->lights.setFlicker(unsignedArg(args, 0), timing);
        }
        return binding::none();
    });

    // `SetWorldAmbient(r, g, b)`.
    // @orig 0x0017f218 LightManager_SetWorldAmbient (LightManager.cpp)
    vm.registerFunction("SetWorldAmbient", [ctx](std::span<const Value> args) -> binding::Results {
        graphics::LevelLighting* lighting = ctx->lighting;
        if (lighting != nullptr) {
            lighting->lights.setWorldAmbient(floatArg(args, 0), floatArg(args, 1), floatArg(args, 2));
        }
        return binding::none();
    });

    // `SetGammaOffset({r, g, b})`.
    // @orig 0x001b4908 LightManager_SetColourOffset (unknown)
    vm.registerFunction("SetGammaOffset", [ctx](std::span<const Value> args) -> binding::Results {
        graphics::LevelLighting* lighting = ctx->lighting;
        if (lighting != nullptr) {
            if (const auto offset = tableArg<3>(args, 0)) {
                lighting->lights.setColourOffset((*offset)[0], (*offset)[1], (*offset)[2]);
            }
        }
        return binding::none();
    });

    // `SetFogColor(r, g, b)`.
    // @orig 0x0040c868 Level_SetFogColour (unknown)
    vm.registerFunction("SetFogColor", [ctx](std::span<const Value> args) -> binding::Results {
        graphics::LevelLighting* lighting = ctx->lighting;
        if (lighting != nullptr) {
            lighting->fog.colour = graphics::fogColourOf(floatArg(args, 0), floatArg(args, 1), floatArg(args, 2));
        }
        return binding::none();
    });

    // `SetFogDistance(fraction)`: where the fog starts, as a fraction of the far clip.
    // @orig 0x0040c908 Level_SetFogDistance (unknown)
    vm.registerFunction("SetFogDistance", [ctx](std::span<const Value> args) -> binding::Results {
        graphics::LevelLighting* lighting = ctx->lighting;
        if (lighting != nullptr) {
            lighting->fog.start = floatArg(args, 0);
        }
        return binding::none();
    });
}

} // namespace coney::script
