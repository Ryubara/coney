// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "human/locomotion.h"

// The sprint and its stamina: L2 held asks for a sprint every update, stamina (record `+0x14a`) drains while the
// human moves at the sprint gait and refills otherwise, with no delay and no threshold.
// Research: docs/research/characters.md#sprint

namespace coney::human {

/// The stamina values the debug menus may edit while the game runs (docs/guides/debug-menu.md#tunables). Each
/// defaults to Rembrandt's researched value; read through staminaTuning().
struct StaminaTuning {
    int maximum = 135;             ///< The power class's `+0x2c` (class 64, Rembrandt): a new human starts full.
    float drainPerSecond = 20.0F;  ///< Lost each second at the sprint gait (`0x005101f4`, CfgBurnRates rate 5).
    float refillPerSecond = 40.0F; ///< Gained each second otherwise (the power class's `+0x2e`).
};

/// The one StaminaTuning the game uses; at its defaults unless a debug menu changed it.
[[nodiscard]] StaminaTuning& staminaTuning();

/// Why the refill gains nothing this update (docs/research/characters.md#sprint): any one blocks it.
struct RefillBlocks {
    Gait gait = Gait::Standing; ///< The gait of the velocity (`+0x1a8`): the sprint gait blocks the refill.
    bool airborne = false;      ///< A jump or a fall (state flags `0x1c00000000`).
    bool sprintHeld = false;    ///< L2 is held: with the run gait it blocks the refill.
};

/// A human's stamina: a whole number from 0 to its maximum, changed by the drain and the refill at a rate a second,
/// with the fraction of a point carried from one update to the next.
class Stamina {
  public:
    /// Full stamina of `maximum` points.
    explicit Stamina(int maximum = staminaTuning().maximum);

    /// The drain: at the sprint gait, with no action flag set (`busy`: record `+0x08` is not 0), stamina loses
    /// `drainPerSecond × seconds`. Returns true when this took it to 0 (the caller then ends the sprint).
    /// @orig 0x002562d0 Human_DrainMeters (unknown)
    bool drain(Gait gait, bool busy, float seconds);
    /// The refill: gains `refillPerSecond × seconds` up to the maximum, unless `blocks` holds a reason not to.
    /// @orig 0x00256a60 Human_RefillMeters (unknown)
    void refill(const RefillBlocks& blocks, float seconds);
    /// Fills it to the maximum (human flag `0x4000000`; Coney's debug menus).
    void fill();

    [[nodiscard]] int value() const { return m_value; }
    [[nodiscard]] int maximum() const { return m_maximum; }

  private:
    // Adds `points` (negative to drain) with the carried fraction; returns the whole points applied.
    int change(float points);

    int m_value;
    int m_maximum;
    float m_fraction = 0.0F; // the part of a point carried (`+0x158`), its sign the last change's direction
};

/// Whether the sprint is asked for this update: the flag is cleared every update and set again only while L2 is held,
/// stamina is not 0 and nothing forbids a sprint (record `+0x08` bit `0x10`).
/// @orig 0x0027ce90 Player_UpdateSprint (unknown)
[[nodiscard]] bool sprintAsked(bool l2Held, int stamina, bool forbidden = false);

} // namespace coney::human
