// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// When the stick moves a human: the locomotion gate the original's player locomotion and fight-stance walk share. It
// reads the record's `+0x08` (the bits the clips playing hold, docs/research/tasks.md#held-flags) and its state code
// `+0x14`. Pure functions, so each mask can be tested on its own.
// Research: docs/research/tasks.md#locomotion-gate

namespace coney::human {

/// Bits of the record `+0x08` the locomotion's own clips hold (docs/research/characters.md#the-record): a move's start
/// clip, the run stop and the climbs, a jump's landing, and 389 `NORMAL_FROM_FIGHT`.
inline constexpr std::uint32_t kFlagStartClip = 0x10000000;
inline constexpr std::uint32_t kFlagRunStop = 0x80000;
inline constexpr std::uint32_t kFlagLanding = 0x1000000;
inline constexpr std::uint32_t kFlagNormalFromFight = 0x40000000;

/// The record `+0x08` bits under which the stick sets no velocity (`0x00241a88`, `0x00241cd4`): `0x80`, `0x800`, the
/// recovery `0x40000`, the run stop's `0x80000`, the landing's `0x1000000` and the start clip's `0x10000000`. The clip
/// alone moves the body then.
inline constexpr std::uint32_t kVelocityGateFlags = 0x110c0880;
/// The record `+0x08` bits that do not make a human busy (`Human_IsBusy`): `0x800`, `0x40000`, `0x100000`,
/// `0x1000000`, `0x10000000` and `0x40000000`. Any other bit does.
inline constexpr std::uint32_t kBusyFlags = 0xaeebf7ff;

/// The record's state codes (`+0x14`) under which the stick sets no velocity: 5 (the locomotion's skid and stop step,
/// and the idle after a block) and 6 (not identified).
inline constexpr int kStateCodeStop = 5;
inline constexpr int kStateCodeSix = 6;

/// What the gate reads of a human.
struct GateInput {
    std::uint32_t flags = 0; ///< The record's `+0x08`.
    int stateCode = 0;       ///< The record's `+0x14`.
    bool airborne = false;   ///< Falling or jumping (state `0x1c00000000`).
    bool attached = false;   ///< Held at another human's place (`+0x280` ≠ -1, `0x00227d28`): held in a grab.
};

/// Whether the whole stick step is skipped, turning included: the human is airborne or attached, or the record's
/// `+0x08` has a bit of kBusyFlags (an attack's phases, the grab bit, the duck, the run stop, a climb). The state
/// word's own busy bits (`0x79b9e1e0f30`) are Coney's fighter states, which the human tests before the gate.
/// @orig 0x00223cb0 Human_IsBusy (unknown)
[[nodiscard]] bool stickBusy(const GateInput& input);

/// Whether the stick's velocity is zero this update: the record's `+0x08` has a bit of kVelocityGateFlags (a clip
/// moves the body by its root), or the state code is 5 or 6.
[[nodiscard]] bool stickVelocityGated(const GateInput& input);

} // namespace coney::human
