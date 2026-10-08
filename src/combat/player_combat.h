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

/// A power move in a grab extends at most this many times (record `+0xbc` < 2).
inline constexpr int kMaxPowerExtensions = 2;

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
    int animSet = 0;                ///< The held object's anim set (SquareInput::heldSet); 0 with nothing in hand.
    bool fightStance = false;       ///< In a fight stance (SquareInput::fightStance).
    bool grabTargetInReach = false; ///< The search found someone to grab for this circle or circle + cross.
    bool snapTarget = false;        ///< The snap's search found a human that is not the target (SquareInput).
    ChainTargets chain;             ///< What the chain's next step reads of the targets (AttackChain::update()).
    bool fromRear = false;          ///< Holding the victim from behind.
    bool wallInReach = false;       ///< A wall within a throw's reach.
    bool victimMuggable = false;    ///< The held victim may be mugged.
    bool victimInPlace = true;      ///< The held victim stands in its place for a move in the hold.
    bool helpless = false;          ///< Reacting to a hit, stunned, down or held: only the meters run.
    /// The record's `+0x08` as the update starts: the bits the clips playing hold (docs/research/tasks.md#held-flags),
    /// an attack's phases among them, which every reader below tests with its own mask.
    std::uint32_t phase = 0;
    std::uint64_t nowMs = 0; ///< Game time, whole milliseconds.
};

/// What one update decided.
struct CombatOutput {
    int startAnim = anim_id::kNone; ///< Start this clip now.
    bool chainStep = false;         ///< startAnim is the chain's next attack, played from its buffer.
    int hitAnim = anim_id::kNone;   ///< An attack's hit landed this update...
    int hitDamage = 0;              ///< ... doing this much (strikeDamage()).
    bool blocking = false;          ///< The block branch ran (state `0x8001`).
    bool rageStarted = false;
    bool grabStarted = false;
    int grappleAnim = anim_id::kNone; ///< A strong grapple: its front strike (657, 649 raging), the grab's connect.
    bool tackleStarted = false;
    bool grabMissed = false; ///< Circle with nobody in reach: the intro, then the miss clip.
    GrabAction grabAction = GrabAction::None;
    MountAction mountAction = MountAction::None; ///< What the player did in the mount (startAnim is its clip).
    bool grabPowerOut = false;             ///< The grab broke because the power meter ran out (grabAction is LetGo).
    GameResult game = GameResult::Running; ///< A minigame's result this update (mugging or theft).
};

/// The player's combat state and its per-update decisions.
class PlayerCombat {
  public:
    /// A fighter whose damage comes from `ranges` (null: hits do no damage), starting at game time `startMs` with full
    /// power and empty rage, its coin flips seeded with `seed`. `ranges` must outlive it.
    PlayerCombat(const AnimRangeList* ranges, std::uint64_t startMs, std::uint32_t seed);

    /// One update, in the original's order: the block, the chain, the meters, the state routes, the commands.
    ///
    /// When input is read again (docs/research/combat.md#input-return, docs/research/tasks.md#readers): with `+0x08`
    /// (CombatInput::phase) in the recovery or the run attack the dispatcher reads nothing past the block; a press for
    /// the attack playing is buffered by the chain (AttackChain); square and cross refuse on kAttackRefusingPhases,
    /// circle on kGrabRefusingPhases; the charge and dive keep their own test. **Coney choice**: the grab's moves and
    /// the mount's strike take square's mask, kAttackRefusingPhases (their routes, `0x0027ec20` and
    /// `Player_UpdateGrabbing`, are not traced for it), so one move in a hold plays out before the next and nothing is
    /// read before the hold stands (the grab's clips hold `0x10`).
    /// @orig 0x0027c120 Player_UpdateActions (unknown)
    CombatOutput update(const CombatInput& input, const CombatTuning& tuning);

    /// Starts a theft minigame (triangle at a car's window: the caller finds the window). `stageTurns` is the stereo
    /// rotation's (stereoStageTurns()); `mashFactor` the mash's Warrior factor.
    void startTheft(TheftKind kind, std::uint64_t nowMs, float stageTurns = 1.0F, float mashFactor = 1.5F);
    /// Ends a theft minigame from outside, with no result (`MiniGame_Abort`: a hit on the player, the cuffed human
    /// gone): back to free.
    /// @orig 0x002325e0 MiniGame_Abort (unknown)
    void abortTheft();
    /// How the last theft minigame ended: GameResult::Running while one runs or none has ended; an aborted one leaves
    /// it Running.
    [[nodiscard]] GameResult theftResult() const { return m_theftResult; }
    /// The grab or tackle is over (the victim broke free, was thrown, the tackle resolved): back to free.
    void release();
    /// A hit took the player out of what it was doing: the attack and its chain are lost and the block ends.
    void interrupt();
    /// The attack's clip has ended: its chain ends with it, as a press is taken again on the first update after the
    /// clip (docs/research/combat.md#input-return).
    void endAttack() { m_chain.cancel(); }
    /// The player holds someone without having grabbed them (the reversal of a grab on it): grabbing.
    void startHolding();
    /// Starts the duck's counter `animId` (617-620) as an attack with its hit timing: the block and any chain end.
    void startCounter(int animId, const CombatTuning& tuning);
    /// The mugging record the next mugging runs with in place of the tuning's: `SetInterrogateParam`'s override
    /// while it is set; nothing for the defaults.
    void setMuggingOverride(const std::optional<MuggingParams>& params) { m_muggingOverride = params; }

    [[nodiscard]] CombatMode mode() const { return m_mode; }
    [[nodiscard]] bool blocking() const { return m_blocking; }
    [[nodiscard]] const AttackChain& chain() const { return m_chain; }
    [[nodiscard]] PowerMeter& power() { return m_power; }
    [[nodiscard]] const PowerMeter& power() const { return m_power; }
    [[nodiscard]] RageMeter& rage() { return m_rage; }
    [[nodiscard]] const RageMeter& rage() const { return m_rage; }
    [[nodiscard]] const std::optional<MuggingGame>& mugging() const { return m_mugging; }
    [[nodiscard]] const std::optional<StereoTheft>& theft() const { return m_theft; }
    [[nodiscard]] const std::optional<ButtonMash>& mash() const { return m_mash; }
    /// The grab's power move playing (57, 63 or an extension), or anim_id::kNone.
    [[nodiscard]] int powerMove() const { return m_powerMove; }

  private:
    // The record's +0x08 for this update.
    [[nodiscard]] static std::uint32_t phaseFlags(const CombatInput& input) { return input.phase; }
    // R1 held in a fight, or L1 released or L1 + R1 short of full rage while blocking: the block. Returns true when
    // the command is R1 held, which ends the update.
    bool updateBlock(const CombatInput& input, CombatOutput& out);
    // Starts `animId` as an attack (through the chain, for its hit timing) and names it in `out`.
    void startAttack(int animId, const CombatTuning& tuning, CombatOutput& out);
    // The grabbing route.
    void updateGrabbing(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out);
    // Cross + square outside a hold: the special 653 (645 in rage), which needs and spends a quarter of the power meter
    // (docs/research/combat.md#attacks). Refused on kSpecialRefusingPhases. The fighter adds 2 from the target's rear
    // (Fighter::playAttack()). **Coney choice**: circle + triangle (the tag) is not built.
    // @orig 0x00287730 Player_Special (unknown)
    void special(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out);
    // Circle + cross outside a hold: the strong grapple, a grab whose connect is the strike 657 (649 in rage), with no
    // power check or cost; with nobody to grab, nothing (docs/research/combat.md#strong-grapple).
    // @orig 0x00263c90 Player_SpecialAttack (unknown)
    void strongGrapple(const CombatInput& input, CombatOutput& out);
    // The mounted route (after a tackle or a grab's mount): updateMount's move, played through the chain or the mode.
    void updateMounting(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out);
    // In a grab, square or cross in the power move's window plays its next part, id + 2 (57 → 59, 63 → 65). Returns
    // whether it did. Research: docs/research/combat.md#grabbing
    // @orig 0x0027df38 Player_UpdatePowerMove (unknown)
    bool extendPowerMove(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out);
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
    std::optional<MuggingParams> m_muggingOverride; // SetInterrogateParam's record, while set
    std::optional<StereoTheft> m_theft;
    std::optional<ButtonMash> m_mash;
    float m_mashFactor = 1.5F;
    GameResult m_theftResult = GameResult::Running;
    int m_powerMove = anim_id::kNone; // the grab's power move playing (57, 63 or an extension), else kNone
    int m_powerExtensions = 0;        // its extensions so far (record +0xbc)
};

} // namespace coney::combat
