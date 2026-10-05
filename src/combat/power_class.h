// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A human's power class: the numbers that decide how it takes hits and how its power meter runs (the record at
// `0x006619a0 + class × 0x44`, docs/research/characters.md#power-classes). Values read at runtime in the street.
// Research: docs/research/combat.md#constants, docs/research/combat.md#power-meter

namespace coney::combat {

/// One power class.
struct PowerClass {
    int powerMax = 400;            ///< `+0x28`: the power meter's maximum.
    int refillPerSecond = 60;      ///< `+0x2a`: the power meter's refill.
    float hurtFraction = 0.3F;     ///< `+0x04`: below this share of its health the human is hurt.
    float hurtPowerFactor = 0.75F; ///< `+0x18`: the power maximum's scale while hurt.
    int stunMs = 200;              ///< `+0x30`: a stun's length.
    int groundMs = 2750;           ///< `+0x34`: how long a knockdown keeps it down.
    int struggleDivisor = 3;       ///< Byte `+0x36`: the grab struggle's divisor.
};

/// The player's class (64, Rembrandt in the street).
inline constexpr PowerClass kPlayerPowerClass{};
/// The street civilian's class (2, `PoizoCiv`).
inline constexpr PowerClass kCivilianPowerClass{.powerMax = 200,
                                                .refillPerSecond = 32,
                                                .hurtFraction = 0.35F,
                                                .hurtPowerFactor = 0.55F,
                                                .stunMs = 750,
                                                .groundMs = 2000,
                                                .struggleDivisor = 4};

} // namespace coney::combat
