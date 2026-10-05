// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "combat/anim_ids.h"
#include "combat/anim_ranges.h"
#include "combat/attacks.h"
#include "combat/combat_tuning.h"
#include "combat/commands.h"
#include "combat/grab.h"
#include "combat/meters.h"
#include "combat/stick.h"
#include "combat/stick_games.h"
#include "human/locomotion.h"

// The player's combat as one update: the block, the chain, the state routes (grabbing, mugging, a theft) and the
// commands that start attacks, grabs, tackles and rage, with the meters moved on. It decides; it does not play clips
// or move anyone: the caller starts the clip it names, applies the hits to their targets and tells it when a grab or
// a tackle ends.
// Research: docs/research/combat.md#dispatch

namespace coney::combat {

/// What the player is doing, as far as combat's routes go (the state flags `0x40`/`0x80`, `0x400`, `0x100`,
/// `0x4000000`).
enum class CombatMode : std::uint8_t { Free, Grabbing, Tackling, Mugging, Theft };

/// The theft minigames by the per-player mode.
enum class TheftKind : std::uint8_t { Mash = 1, Rotate = 3 };

/// One update's input to combat.
struct CombatInput {
    CommandId command = command::kNone; ///< From CommandMatcher.
    std::uint16_t buttons = 0;          ///< The held buttons (the block reads R1 itself).
    Stick stick;                        ///< The left stick in the player's frame (x right, y ahead).
    Stick padStick;                     ///< The left stick as the pad reads it (x right, y up): the minigames'.
    human::Gait gait = human::Gait::Standing;
    bool inFight = true;                  ///< The fight test (`0x00224f28`, not researched) that lets R1 block.
    TargetKind target = TargetKind::None; ///< What square is aimed at.
    float objectHeight = 0.0F;            ///< A breakable target's point above the feet, metres.
    bool grabTargetInReach = false;       ///< nearestTarget() found someone within grabSearchRange() for this circle.
    bool fromRear = false;                ///< Holding the victim from behind.
    bool wallInReach = false;             ///< A wall within a throw's reach.
    bool victimMuggable = false;          ///< The held victim may be mugged.
    bool victimInPlace = true;            ///< The held victim stands in its place for a move in the hold.
    std::uint64_t nowMs = 0;              ///< Game time, whole milliseconds.
};

/// What one update decided.
struct CombatOutput {
    int startAnim = anim_id::kNone; ///< Start this clip now.
    int hitAnim = anim_id::kNone;   ///< An attack's hit landed this update...
    int hitDamage = 0;              ///< ... doing this much (strikeDamage()).
    bool blocking = false;          ///< The block branch ran (state `0x8001`).
    bool rageStarted = false;
    bool grabStarted = false;
    bool tackleStarted = false;
    bool grabMissed = false; ///< Circle with nobody in reach: the intro, then the miss clip.
    GrabAction grabAction = GrabAction::None;
    GameResult game = GameResult::Running; ///< A minigame's result this update (mugging or theft).
};

/// The player's combat state and its per-update decisions.
class PlayerCombat {
  public:
    /// A fighter whose damage comes from `ranges` (null: hits do no damage), starting at game time `startMs` with full
    /// power and empty rage, its coin flips seeded with `seed`. `ranges` must outlive it.
    PlayerCombat(const AnimRangeList* ranges, std::uint64_t startMs, std::uint32_t seed);

    /// One update, in the original's order: the block, the chain, the meters, the state routes, the commands.
    /// @orig 0x0027c120 Player_UpdateActions (unknown)
    CombatOutput update(const CombatInput& input, const CombatTuning& tuning);

    /// Starts a theft minigame (triangle at a car's window: the caller finds the window). `stageTurns` is the stereo
    /// rotation's (stereoStageTurns()); `mashFactor` the mash's Warrior factor.
    void startTheft(TheftKind kind, std::uint64_t nowMs, float stageTurns = 1.0F, float mashFactor = 1.5F);
    /// The grab or tackle is over (the victim broke free, was thrown, the tackle resolved): back to free.
    void release();

    [[nodiscard]] CombatMode mode() const { return m_mode; }
    [[nodiscard]] bool blocking() const { return m_blocking; }
    [[nodiscard]] const AttackChain& chain() const { return m_chain; }
    [[nodiscard]] PowerMeter& power() { return m_power; }
    [[nodiscard]] const PowerMeter& power() const { return m_power; }
    [[nodiscard]] RageMeter& rage() { return m_rage; }
    [[nodiscard]] const RageMeter& rage() const { return m_rage; }
    [[nodiscard]] const std::optional<MuggingGame>& mugging() const { return m_mugging; }
    [[nodiscard]] const std::optional<StereoTheft>& theft() const { return m_theft; }

  private:
    // R1 held in a fight, or L1 released or L1 + R1 short of full rage while blocking: the block. Returns true when
    // the command is R1 held, which ends the update.
    bool updateBlock(const CombatInput& input, CombatOutput& out);
    // Starts `animId` as an attack (through the chain, for its hit timing) and names it in `out`.
    void startAttack(int animId, const CombatTuning& tuning, CombatOutput& out);
    // The grabbing route.
    void updateGrabbing(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out);
    // The theft route.
    void updateTheft(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out);
    /// Circle tapped or held: a grab or a tackle, or a miss.
    /// @orig 0x00284920 Player_GrabOrTackle (unknown)
    void grabOrTackle(const CombatInput& input, CombatOutput& out);
    // The commands of a free player.
    void updateCommands(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out);

    const AnimRangeList* m_ranges;
    CombatRandom m_random;
    AttackChain m_chain;
    PowerMeter m_power;
    RageMeter m_rage;
    CombatMode m_mode = CombatMode::Free;
    bool m_blocking = false;
    std::optional<MuggingGame> m_mugging;
    std::optional<StereoTheft> m_theft;
    std::optional<ButtonMash> m_mash;
    float m_mashFactor = 1.5F;
};

} // namespace coney::combat
