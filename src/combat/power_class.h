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
    // What only an AI reads (docs/research/ai.md#block, docs/research/ai.md#attack-action). The attack delay's factors
    // are Rembrandt's as read at runtime; the three chances are **Coney choices** until the disc's are read (the
    // configuration's `CfgPowerClass`, ai::AiConfig).
    float blockChance = 0.5F;           ///< `+0x08`: the chance to block, times the base chance to block.
    float hurtBlockChance = 0.25F;      ///< `+0x0c`: the same while hurt.
    float attackDelayFactor = 3.0F;     ///< `+0x1c`: `CfgAttackDelay`'s factor.
    float attackDelayDownFactor = 3.0F; ///< `+0x20`: the same against a downed target.
    float counterChance = 0.1F;         ///< `+0x24`, 0-1: the chance, each update of a block, to press R1.
    /// Byte `+0x40`: the health rings span byte / 100 of their circle (35 for Rembrandt's class 64, read at runtime;
    /// what the field is meant as is open, docs/research/hud.md#the-health-rings).
    int ringByte = 35;
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
                                                .struggleDivisor = 4,
                                                .blockChance = 0.5F,
                                                .hurtBlockChance = 0.25F,
                                                .attackDelayFactor = 3.0F,
                                                .attackDelayDownFactor = 3.0F,
                                                .counterChance = 0.1F,
                                                // **Coney's stand-in**: the civilian's byte is not read; Rembrandt's.
                                                .ringByte = 35};

} // namespace coney::combat
