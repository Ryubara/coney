// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>

// The light types (script types of kind 0x04): a point light whose colour cross-fades between two colours, stepped
// by its type's update (the alarm's strober, the neon signs' light).
// Research: docs/research/script-types.md#light-type-lights

namespace coney::effects {

/// Which light type a LightTask runs.
enum class LightKind : std::uint8_t {
    Strober, ///< `strober`: the store alarm's red pulse (docs/research/script-types.md#strober).
    Neon,    ///< `sub_neon_light`: a neon sign's steady light with its off spells (#neon-signs).
};

/// One light type's task: the light record's two colours (`+0x80`, the colour faded from, and `+0x84`, the one faded
/// to, both `0xRRGGBBAA`), its update interval and the type's own state. Stepped one 60 Hz tick at a time, so it runs
/// the same at any frame rate.
struct LightTask {
    LightKind kind = LightKind::Strober;
    std::uint32_t from = 0;   ///< `+0x80`: the colour at the last update.
    std::uint32_t to = 0;     ///< `+0x84`: the colour faded to by the next.
    std::uint32_t colour = 0; ///< Neon data `+0x08`: the sign's colour.
    float radius = 0.0F;      ///< `+0x8c`, metres.
    bool lightsWorld = false; ///< Task flag `0x08`: the world is lit as well as objects and humans.
    int interval = 2;         ///< Ticks between updates (`LightTask_Init`'s 2 until the type sets its own).
    int since = 0;            ///< Ticks since the last update.
    int counter = 0;          ///< The strober's step (1-4); the neon's counter (data `+0x04`).
    bool offSpell = false;    ///< The neon's state (the attach-point byte `+0x6c`): 1 in an off spell.
    bool ending = false;      ///< Its on flag is cleared (message `0x13`): the next update ends it.
    bool done = false;        ///< Ended: it gives no light.
};

/// A whole number in [0, n] (the game's `Random_Int(n)`, inclusive).
using LightRandom = std::function<std::uint32_t(std::uint32_t n)>;

/// A `strober` as `Strober_Init` makes it: red `0xff0000ff` (both colours), radius 10 m, an update every 10 ticks,
/// lighting the world too (flags 8).
/// @orig 0x003e8408 Strober_Init (unknown)
[[nodiscard]] LightTask makeStrober();

/// A neon sign's `sub_neon_light` of colour `colour` (`0xRRGGBBAA`) as its sign's init makes it: radius 4 m, lighting
/// the world too (flags `0x488`), an update every 60 ticks, faded from `colour` to black (the target `LightTask_Init`
/// left), counter 0 and steady.
/// @orig 0x003e02a8 SubNeonLight_Init (unknown)
[[nodiscard]] LightTask makeNeonLight(std::uint32_t colour);

/// One 60 Hz tick: the time since the last update grows, and when it reaches the interval the update runs
/// (`LightTask_Process`: the target becomes the colour faded from, then the type's update sets what comes next).
/// `random` is drawn by the neon's off spell.
/// @orig 0x00390970 LightTask_Process (unknown)
void tickLight(LightTask& task, const LightRandom& random);

/// A light's colour, each channel 0-1.
struct LightColourNow {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

/// The colour the light shines with now (`LightTask_Update`): from `from` to `to` linearly over the interval, by the
/// ticks since the last update (clamped at the target).
/// @orig 0x003906b8 LightTask_Update (unknown)
[[nodiscard]] LightColourNow lightColour(const LightTask& task);

} // namespace coney::effects
