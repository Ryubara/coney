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

    /// Lock-on (`CfgLockOn`, auto-combat, `CfgAutoLock`, `CfgAutoLockAndCombat`: 0, 0, 0, 1 in the street): L1 held
    /// locks only with the first; any of the others locks whenever there is a target.
    bool lockOnButton = false;
    bool autoCombat = false;
    bool autoLock = false;
    bool autoLockAndCombat = true;
    /// A target farther than this is dropped (`0x005104c0`, 2.5 m), unless L1 or a hold keeps it.
    float targetDropDistance = 2.5F;
    /// The combat walk's speed, m/s, at any stick deflection past the dead zone (the clips 380-387 cover 2.4 m in
    /// 0.7 s).
    float combatWalkSpeed = 3.429F;

    /// Turning a grab: the stick length it needs (`0x005102e8`), the largest turn an update for a player (radians,
    /// table `0x005101b0`), the share of the last turn carried on (`0x00510314`) and the one when the turn reverses
    /// within 45°; and the pair's backward walk in the front and the rear hold, m/s.
    float grabTurnStick = 0.95F;
    float grabTurnMax = 0.192F;
    float grabTurnCarry = 0.8F;
    float grabTurnReverseCarry = -0.5F;
    float grabWalkFront = 1.125F;
    float grabWalkRear = 1.22F;

    /// Being hit: the counter at a grab's catch (`0x00510254`, 1) and the health floor of a player (`0x0051024c`, a
    /// share of its maximum one hit cannot go below).
    bool grabCounters = true;
    float healthFloor = 0.25F;
};

/// The one CombatTuning the game uses; at its defaults unless a debug menu changed it.
[[nodiscard]] CombatTuning& combatTuning();

} // namespace coney::combat
