// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "animation/anim_math.h"
#include "combat/anim_ranges.h"
#include "combat/commands.h"
#include "combat/player_combat.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "human/target_human.h"

// The player's combat inside the human: combat::PlayerCombat decides each update from the command, the stick, the gait
// and the targets around; the fighter plays what it decided through the human's animator (the attacks, their chains,
// the block, the grab, the tackle, the throws, the mugging), lands the hits on the targets with the victim's clips,
// and tells the human when combat, not the stick, moves the body.
// Research: docs/research/combat.md, docs/research/combat.md#grab-posing (the pair's placement: human/pair_placement.h)

namespace coney::human {

/// How a grab's two bodies are held together (docs/research/combat.md#grab-posing).
enum class PairStage : std::uint8_t {
    None,     ///< No grab, or a tackle's intro.
    Intro,    ///< The grab's intro (71) plays; the victim waits.
    Moving,   ///< Paired clips that carry both bodies play (the connecting 72 / 73 or 74 / 75, a spin 78 / 79 or
              ///< 80 / 81): each body is moved by its own root motion until the snap at their end.
    Attached, ///< The victim stands at its stored offset in the grabber's frame, placed every update.
};

/// A strike reaches a target within the attack's far range (combat::AnimRangeList::farRange()), or this far when the
/// list has none (**Coney's choice**), and in front of the attacker (within 90° of its facing).
inline constexpr float kDefaultStrikeReach = 2.0F;
/// The target search (`Player_PickTarget`): humans within the range × 1.1 and this many degrees of the stick's heading
/// (or the facing with the stick at rest), else within the range × 0.9 at any angle.
inline constexpr float kPickConeDegrees = 54.0F;
inline constexpr float kPickConeScale = 1.1F;
inline constexpr float kPickAnyScale = 0.9F;
/// The stick length above which its heading leads the search.
inline constexpr float kPickStick = 0.01F;
/// Targets more than this far above or below are skipped.
inline constexpr float kPickHeight = 2.0F;
/// Beyond the attack's far range the attacker only turns, at most this much (**Coney's reading** of the research's
/// "capped at 8": degrees).
inline constexpr float kAttackTurnCapDegrees = 8.0F;

/// What the fighter is given each update.
struct FighterInput {
    combat::CommandId command = combat::command::kNone; ///< From the command matcher.
    std::uint16_t buttons = 0;                          ///< The held buttons.
    combat::Stick stick;                                ///< The camera-turned stick in the facing frame.
    combat::Stick padStick;                             ///< The stick as the pad reads it (the minigames').
    Gait gait = Gait::Standing;
    anim::Vec3 position;                   ///< The player's feet.
    float heading = 0.0F;                  ///< The player's heading, radians.
    std::uint64_t nowMs = 0;               ///< Game time, whole milliseconds.
    std::span<TargetHuman* const> targets; ///< The humans that can be fought.
};

/// The player's combat, played through its animator.
class Fighter {
  public:
    /// A fighter whose damage comes from `ranges` (may be null: no damage), coin flips seeded with `seed`.
    explicit Fighter(const combat::AnimRangeList* ranges, std::uint32_t seed = 1);

    /// One update: a grab's alignment turns and its pair's moments (the connect, the snap at a clip's end), then
    /// combat decides, the fighter plays the clips, turns `heading` to a target it attacks, grabs or tackles, lands the
    /// hits, and puts an attached victim at its offset.
    void update(const FighterInput& input, HumanAnimator& animator, float& heading);

    /// Whether combat moves the body this update rather than the stick: blocking, holding someone, mugging, a theft,
    /// or an attack's clips playing.
    [[nodiscard]] bool holdsMovement(const HumanAnimator& animator) const;
    [[nodiscard]] bool blocking() const { return m_combat.blocking(); }
    [[nodiscard]] const combat::PlayerCombat& combat() const { return m_combat; }
    [[nodiscard]] combat::PlayerCombat& combat() { return m_combat; }
    /// The output of the last update.
    [[nodiscard]] const combat::CombatOutput& last() const { return m_last; }
    /// The victim held in a grab, a tackle or a mugging; null when none.
    [[nodiscard]] const TargetHuman* held() const { return m_held; }
    /// The hold is from the victim's rear (a grab from behind, after a spin or in the mugging).
    [[nodiscard]] bool fromRear() const { return m_rear; }
    /// How the grab's two bodies are held together now.
    [[nodiscard]] PairStage pairStage() const { return m_pair; }
    /// The velocity (m/s, horizontal) an attack's start slides the body at this update, counting the slide down; zero
    /// when none. The human calls it once per update while combat holds the movement.
    [[nodiscard]] anim::Vec3 takeSlide();
    /// The target human the search would pick for an attack of `range` metres (null for none).
    /// @orig 0x0027a6c0 Player_PickTarget (unknown)
    [[nodiscard]] static TargetHuman* pickTarget(const FighterInput& input, float range);
    /// Hits that reached a target, and the damage they did.
    [[nodiscard]] int hitsLanded() const { return m_hitsLanded; }
    [[nodiscard]] int damageDealt() const { return m_damageDealt; }

  private:
    // The nearest standing (or grounded, with `grounded`) target within `range` in front of the player.
    [[nodiscard]] TargetHuman* inFront(const FighterInput& input, float range, bool grounded) const;
    // The strike reach of attack `animId`.
    [[nodiscard]] float reachOf(int animId) const;
    // Plays an attack the dispatcher started (`animId`), with what follows it, turning and sliding to its target.
    void playAttack(int animId, const FighterInput& input, HumanAnimator& animator, float& heading);
    // Turns (and within the far range slides) towards the target of attack `animId`, as Attack_Start steers.
    void steer(int animId, const FighterInput& input, float& heading);
    // The block's clip: the shuffle with the stick pushed, the sustain otherwise.
    static void playBlock(const FighterInput& input, HumanAnimator& animator);
    // A grab or tackle started on `victim`: the intro plays (the player turning to face it), the victim waits.
    void startHold(TargetHuman& victim, const FighterInput& input, float& heading, bool tackle,
                   HumanAnimator& animator);
    // The pair's moments, read from the grabber's clip at the start of an update: the connecting clip starting (the
    // alignment and the victim's paired clip), a connecting clip or a spin ending (the gate and the snap), and a spin
    // starting (detachForSpin()).
    void followPairClips(const FighterInput& input, HumanAnimator& animator, float heading);
    // A spin has started: the victim leaves its offset and moves by its own clip until the spin's end.
    void detachForSpin(const HumanAnimator& animator);
    // The connecting clip has started: align the pair over the alignment's time and start the victim's clip, or fail
    // the grab beyond the far range.
    // @orig 0x0026be68 Grab_Connect (unknown)
    void connect(const FighterInput& input, HumanAnimator& animator, float heading);
    // Puts the victim at `offset` in the grabber's frame, turned by `turn` from the grabber's heading, and attaches it
    // there (the offset stored).
    // @orig 0x00276d98 Pair_SnapAttach (unknown)
    // @orig 0x002802a0 Pair_Attach (unknown)
    void snapAttach(const FighterInput& input, float heading, anim::Vec3 offset, float turn);
    // One update of the alignment's turns: the grabber's (on `heading`) and the victim's.
    void stepAlignment(float& heading);
    // Whether the held victim stands in its place for a move in the hold (the current hold's point).
    [[nodiscard]] bool victimInPlace(const FighterInput& input) const;
    // The moves inside a grab, with the victim's clips.
    void playGrabAction(const combat::CombatOutput& out, HumanAnimator& animator);
    // Lands a hit of attack `animId` and `damage`.
    void landHit(int animId, int damage, const FighterInput& input);
    // Lets go of the victim: the let-go clips (95 / 94) with `letGo`, else straight to the idles (a release, the
    // victim out of health).
    void releaseHold(HumanAnimator& animator, bool letGo);
    // Puts an attached victim at its stored offset from the grabber at `position` facing `heading`.
    // @orig 0x00244e78 Human_MoveAttached (unknown)
    void placeAttached(anim::Vec3 position, float heading) const;

    const combat::AnimRangeList* m_ranges;
    combat::PlayerCombat m_combat;
    combat::CombatOutput m_last;
    TargetHuman* m_held = nullptr;
    TargetHuman* m_thrown = nullptr;    // the victim of the throw whose hit has not landed yet
    TargetHuman* m_candidate = nullptr; // what the grab or tackle search found this update
    bool m_tacklePending = false;       // the tackle's intro plays; the victim reacts when its hit clip starts
    bool m_mugOnTarget = false;
    bool m_rear = false;    // the hold is from the victim's rear
    anim::Vec3 m_slide;     // an attack start's (or a grab's alignment's) slide velocity, m/s
    int m_slideUpdates = 0; // updates of slide left
    PairStage m_pair = PairStage::None;
    anim::Vec3 m_holdOffset;       // the attached victim's place in the grabber's frame
    float m_holdTurn = 0.0F;       // the attached victim's heading less the grabber's
    std::uint32_t m_lastClip = 0;  // the grabber's clip at the end of the last update
    float m_turnStep = 0.0F;       // the grabber's alignment turn per update, radians
    float m_victimTurnStep = 0.0F; // the victim's
    int m_turnUpdates = 0;         // updates of turn left
    bool m_wasBlocking = false;
    int m_hitsLanded = 0;
    int m_damageDealt = 0;
};

} // namespace coney::human
