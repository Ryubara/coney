// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The combat values the debug menus may edit while the game runs, each defaulting to the value read from the original
// in the street (docs/research/combat.md#constants). The game reads them through combatTuning(); the tunables registry
// changes them only between two steps (src/debug/combat_tunables.h), so a run stays deterministic.
// Research: docs/research/combat.md

namespace coney::combat {

/// The combat values, at the original's defaults. Times are in updates of 1/30 s or in milliseconds of game time, as
/// the original keeps them.
struct CombatTuning {
    /// Samples a history hold needs: circle held this long tackles, triangle this long gives 0xb (`0x0050b708`, 7 at
    /// runtime; the file says 5).
    int historyHoldSamples = 7;

    /// Square snaps to a side or back attack when the stick is beyond kSnapStick (`CfgSnap`, 1).
    bool snapAttacks = true;

    /// The chain attacks' timing, in updates from the press, read from `S1` at runtime: the hit, the chain window's
    /// opening and closing, the start of the recovery and the attack's end (the fight idle returns).
    /// **Coney choice**: every chain attack uses `S1`'s timing until the others are measured, and the end phase
    /// (`0x4`) lasts from the window's close to the recovery at 17 (the research has only "then 0x4 and recovery").
    int hitUpdate = 2;
    int chainOpenUpdate = 6;
    int chainCloseUpdate = 15;
    int recoveryUpdate = 17;
    int attackEndUpdate = 20;

    /// The grab and tackle search: the far range of anim 70 (grab) or 3 (tackle) times this (`0x00284920`).
    float grabSearchScale = 1.25F;

    /// The power fraction a power strike needs and a throw costs (`CfgPowerEndurance`, 0.25).
    float powerEndurance = 0.25F;
    /// The fraction of the power meter's maximum a grab strike costs before a player's halving (`0x00510998`, 0.2).
    float grabStrikeCost = 0.2F;
    /// The power meter's drain while grabbing or tackling, per second (1 every 2 updates).
    float powerDrainPerSecond = 15.0F;

    /// `CfgRagePoints`: the points up to which a hit's rage counts in full, and the factors below and above it.
    float ragePointsCap = 25.0F;
    float rageFactorBelow = 1.0F;
    float rageFactorAbove = 0.1F;
    /// The rage meter's drain while raging, per second: the maximum × Warrior byte `+0x04` (240 %) / 100 per 20 s.
    float rageDrainPerSecond = 9.36F;
    /// How long a gain holds the meter before it decays (`CfgRageHandlers`), ms, and the decay after it, per second:
    /// the maximum × Warrior byte `+0x03` (200 %) / 100 per 20 s.
    int rageHoldMs = 5000;
    float rageDecayPerSecond = 7.8F;

    /// `CfgButtonMash`: the meter total that completes a mash, its loss each update, and a press's gain (used
    /// halved, then scaled by the Warrior class's 1.5 or 0.7).
    int mashTarget = 1000;
    int mashDecay = 15;
    int mashPressGain = 250;

    /// The mugging: the stick length that counts, the time on target it needs, how often the target angle moves, the
    /// time after which it fails (ms), and the on-target tolerance (degrees). **Coney choice**: of the two tolerances
    /// read (50° and 60°) the first applies.
    float muggingStick = 0.5F;
    int muggingRequiredMs = 5000;
    int muggingPeriodMs = 2500;
    int muggingFailMs = 50000;
    float muggingToleranceDegrees = 50.0F;

    /// The stereo theft (mode 3): the stick length both samples need, the largest turn an update may make (degrees),
    /// the pause after a stage (ms) and the stages to succeed.
    float theftStick = 0.8F;
    float theftMaxStepDegrees = 90.0F;
    int theftStagePauseMs = 250;
    int theftStages = 4;
};

/// The one CombatTuning the game uses; at its defaults unless a debug menu changed it.
[[nodiscard]] CombatTuning& combatTuning();

} // namespace coney::combat
