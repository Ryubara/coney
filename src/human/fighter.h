// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "animation/anim_math.h"
#include "characters/anim_set.h"
#include "combat/anim_ranges.h"
#include "combat/being_hit.h"
#include "combat/commands.h"
#include "combat/grabbed.h"
#include "combat/meters.h"
#include "combat/player_combat.h"
#include "combat/rage_awards.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "human/target_human.h"
#include "human/victim.h"

// The player's combat inside the human: combat::PlayerCombat decides each update from the command, the stick, the gait
// and the targets around; the fighter plays what it decided through the human's animator (the attacks, their chains,
// the block, the grab, the tackle, the throws, the mugging), lands the hits on the targets with the victim's clips,
// keeps the target it is locked onto, and tells the human when combat, not the stick, moves the body. It is also the
// player's victim side: the hits, warnings and grabs another human aims at it.
// The work is split by subject: fighter.cpp (the update, the attacks and the targets), fighter_grab.cpp (posing and
// playing a grab the player holds) and fighter_victim.cpp (the player hit, warned and grabbed).
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
/// The player's health (Rembrandt's 900 at runtime, record `+0x146`).
inline constexpr int kPlayerHealth = 900;

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

/// What an attacker's clip tells the player before its hit (combat::warningBetween() finds it in the attacker's
/// clip; the attacker sends it when the player is within twice the attack's reach).
struct AttackNotice {
    combat::AttackWarning warning = combat::AttackWarning::Duck;
    int attackAnim = -1; ///< The attacker's anim id.
    int code = 0;        ///< The attack's hit code (for an early block reaction).
    anim::Vec3 attacker; ///< Where the attacker's feet are.
};

/// A grab another human has caught the player in, at the end of its intro (docs/research/combat.md#grabbed).
struct GrabCatch {
    const characters::AnimSet* grabberAnims = nullptr; ///< The grabber's set: the player plays its reaction clips.
    bool fromRear = false;                             ///< The grabber holds the player from behind.
    combat::GrabberState grabber;                      ///< The grabber's numbers now.
};

/// What the grabbed player did to its grabber this update, for the grabber to apply.
struct GrabbedReport {
    combat::GrabbedAction action = combat::GrabbedAction::None;
    bool countered = false;   ///< R1 at the catch: the player's counter 76 plays instead of the grab.
    int grabberClip = -1;     ///< The clip the grabber plays (the player's move's id + 1, from the player's set).
    int grabberPowerCost = 0; ///< Power the grabber loses.
    int grabberDamage = 0;    ///< Damage the grabber takes (the player's move's Anim Range List damage).
    bool grabberKnockedDown = false; ///< The grabber goes down (an escape)...
    bool grabberStunned = false;     ///< ... and is stunned (an escape, a counter).
    bool ended = false;              ///< The player is out of the grab.
};

/// The player's combat, played through its animator.
class Fighter {
  public:
    /// A fighter whose damage comes from `ranges` (may be null: no damage), coin flips seeded with `seed`, with the
    /// player's power class and kPlayerHealth.
    explicit Fighter(const combat::AnimRangeList* ranges, std::uint32_t seed = 1);

    /// One update: the hits, warnings and grab taken since the last update (the victim's side), the timers of a
    /// reaction, then a grab's alignment turns and its pair's moments (the connect, the snap at a clip's end), then
    /// combat decides, the fighter plays the clips, turns `heading` to a target it attacks, grabs or tackles, lands the
    /// hits, keeps or drops its target, and puts an attached victim at its offset.
    void update(const FighterInput& input, HumanAnimator& animator, float& heading);

    /// Whether combat moves the body this update rather than the stick: blocking, holding someone or held, mugging, a
    /// theft, an attack's clips playing, or reacting to a hit (stunned, down, getting up).
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

    /// The target kept (human `+0xc8`); null when none.
    [[nodiscard]] const TargetHuman* target() const { return m_target; }
    /// The target the fight stance is locked onto (combat::lockedOn()): it has one and the lock-on settings lock; null
    /// otherwise. Locked, the human faces it every update and walks in the combat walk.
    [[nodiscard]] const TargetHuman* lockTarget() const;
    /// The grabbing player's movement: with the hold standing still (no move playing) and the stick beyond
    /// CombatTuning::grabTurnStick, turns `heading` towards the stick's heading `stickHeading` + 180° (the grabber's
    /// back to the stick) by combat::grabTurnStep() and returns the pair's backward walk (m/s, world axes); zero
    /// otherwise.
    /// @orig 0x00245310 Human_MoveGrabbing (unknown)
    [[nodiscard]] anim::Vec3 moveGrab(float stickHeading, float stickMagnitude, const HumanAnimator& animator,
                                      float& heading);

    // The player's victim side: the entry points another human (a future AI attacker) and the tests use.

    /// A hit on the player, applied at its next update (the update keeps its largest).
    void takeHit(const IncomingHit& hit) { m_victim.hit(hit); }
    /// An attacker's clip warns the player of its hit, acted on at its next update: a blocking player ducks (616,
    /// the attack passing over it) or plays its block reaction early.
    void warn(const AttackNotice& notice) { m_notice = notice; }
    /// Another human's grab catches the player (the end of its intro), acted on at its next update with that update's
    /// command: R1 pressed counters it (combat::counterAtCatch()), else the player is held.
    void catchInGrab(const GrabCatch& grab) { m_catch = grab; }
    /// The grabber's numbers this update, while the player is held.
    void updateGrabber(const combat::GrabberState& grabber);
    /// The grabber lets go of the player (its let-go 95, or a break): the player plays 94 and is free.
    void releaseFromGrab(HumanAnimator& animator);
    /// What the player did to its grabber in the last update.
    [[nodiscard]] const GrabbedReport& grabbedReport() const { return m_report; }
    /// Held in another human's grab.
    [[nodiscard]] bool grabbed() const { return m_grabbed.has_value(); }
    /// The player's health (record `+0x144`).
    [[nodiscard]] const combat::Health& health() const { return m_health; }
    [[nodiscard]] combat::Health& health() { return m_health; }
    /// The player's reactions, stun and ground timers.
    [[nodiscard]] const Victim& victim() const { return m_victim; }
    /// Hurt: below the power class's hurt fraction of its health.
    [[nodiscard]] bool hurt() const { return m_health.fraction() < m_victim.powerClass().hurtFraction; }
    /// Reacting to a hit, stunned, down or getting up: the player cannot act.
    [[nodiscard]] bool helpless(const HumanAnimator& animator) const;
    /// Hits that reached the player, blocked, ducked under, or taken under the hit armour (no reaction).
    [[nodiscard]] int hitsTaken() const { return m_hitsTaken; }
    [[nodiscard]] int hitsBlocked() const { return m_hitsBlocked; }
    [[nodiscard]] int hitsDucked() const { return m_hitsDucked; }
    [[nodiscard]] int hitsArmoured() const { return m_hitsArmoured; }
    /// Duck counters played (617-620).
    [[nodiscard]] int duckCounters() const { return m_duckCounters; }
    /// The repeat tracker of the player's hits.
    [[nodiscard]] const combat::RepeatTracker& repeats() const { return m_repeat; }

  private:
    // --- fighter.cpp: the update, the attacks and the targets.

    // The nearest standing (or grounded, with `grounded`) target within `range` in front of the player.
    [[nodiscard]] TargetHuman* inFront(const FighterInput& input, float range, bool grounded) const;
    // The strike reach of attack `animId`.
    [[nodiscard]] float reachOf(int animId) const;
    // The dispatcher's input from the world: square's target, circle's search, the hold.
    [[nodiscard]] combat::CombatInput combatInput(const FighterInput& input, bool helpless);
    // Plays what the dispatcher decided: the block, rage, a grab or tackle, the grab's moves, the mugging, a theft,
    // an attack.
    void playDecisions(const combat::CombatOutput& out, combat::CombatMode before, const FighterInput& input,
                       HumanAnimator& animator, float& heading);
    // Plays an attack the dispatcher started (`animId`), with what follows it, turning and sliding to its target.
    void playAttack(int animId, const FighterInput& input, HumanAnimator& animator, float& heading);
    // Turns (and within the far range slides) towards the target of attack `animId`, as Attack_Start steers.
    void steer(int animId, const FighterInput& input, float& heading);
    // The block's clip: the shuffle with the stick pushed, the sustain otherwise.
    static void playBlock(const FighterInput& input, HumanAnimator& animator);
    // Lands a hit of attack `animId` and `damage`, and gives the rage it earns.
    void landHit(int animId, int damage, const FighterInput& input);
    // Remembers the clip playing at the end of the update and its time (the pair's moments, the duck's window).
    void noteClip(const HumanAnimator& animator);
    // Keeps the target or drops it (gone, out of health, down, or too far without L1 or a hold), and lets L1 pick one.
    void trackTarget(const FighterInput& input);

    // --- fighter_grab.cpp: a grab the player holds.

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
    // The tackle's hit clip has started: the victim goes down under the player at the mount's offset.
    void mountVictim(const FighterInput& input, const HumanAnimator& animator, float heading);
    // The grab broke at 0 power with the player hurt: the victim escapes (100 / 112), the player is knocked down and
    // stunned (101 / 113 from the victim's set).
    // @orig 0x0026cc18 Grab_Escape (unknown)
    void victimEscapes(const FighterInput& input, HumanAnimator& animator);
    // Lets go of the victim: the let-go clips (95 / 94) with `letGo`, else straight to the idles (a release, the
    // victim out of health).
    void releaseHold(HumanAnimator& animator, bool letGo);
    // Puts an attached victim at its stored offset from the grabber at `position` facing `heading`.
    // @orig 0x00244e78 Human_MoveAttached (unknown)
    void placeAttached(anim::Vec3 position, float heading) const;

    // --- fighter_victim.cpp: the player hit, warned and grabbed.

    // The warning and the hit taken since the last update, as Human_ApplyPendingDamage takes them: a duck lets the
    // attack pass, a held block cancels it, the health floor, the hit armour, then the reaction.
    void takePending(const FighterInput& input, HumanAnimator& animator);
    // Acts on an attacker's warning: a blocking player ducks or plays its block reaction early.
    // @orig 0x00245920 Human_HandleMessage (unknown)
    // @orig 0x00254e78 Human_UpdateBlockState (unknown)
    void takeNotice(const FighterInput& input, HumanAnimator& animator);
    // The duck's counter: square or cross while the duck's own events 0x25 fire asks for it (record +0x14 = 0xe);
    // on the next update 617-620 plays by the side the attacker stands on. Returns whether this update's command
    // went to it.
    // @orig 0x002617f8 Block_DuckCounter (unknown)
    bool duckCounter(const FighterInput& input, HumanAnimator& animator);
    // The reaction's frame of the player now.
    [[nodiscard]] VictimFrame frame(const FighterInput& input) const;
    // The timers of a reaction (the stun's end, the rise) and the mash while down; returns whether the player is
    // still helpless.
    bool stepVictim(const FighterInput& input, HumanAnimator& animator);
    // A grab caught the player this update: a counter, or the held player's clips.
    void startGrabbed(const FighterInput& input, HumanAnimator& animator);
    // One update held in a grab: the struggle, the strike back, the escape, the reversal, or the grabber out of power.
    void updateGrabbed(const FighterInput& input, HumanAnimator& animator);
    // The player breaks out of the grab with `clip` (an escape: knocked-down grabber), taking the clip's damage.
    void escapeGrab(int clip, HumanAnimator& animator);

    const combat::AnimRangeList* m_ranges;
    combat::PlayerCombat m_combat;
    combat::CombatOutput m_last;
    TargetHuman* m_held = nullptr;
    TargetHuman* m_thrown = nullptr;    // the victim of the throw whose hit has not landed yet
    TargetHuman* m_candidate = nullptr; // what the grab or tackle search found this update
    TargetHuman* m_target = nullptr;    // the target kept (human +0xc8)
    bool m_l1Held = false;              // L1 held this update (record +0x00 0x8)
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
    float m_grabTurn = 0.0F;       // the grab's stick turn of the last update, radians
    bool m_wasBlocking = false;
    int m_hitsLanded = 0;
    int m_damageDealt = 0;

    // The victim side.
    combat::Health m_health{kPlayerHealth};
    Victim m_victim;
    bool m_reacting = false;   // a hit's reaction (or an escape, a let-go) holds the player until it is over
    bool m_justCaught = false; // the grab caught the player this update
    std::optional<AttackNotice> m_notice;
    std::optional<GrabCatch> m_catch;
    std::optional<GrabCatch> m_grabbed; // the grab holding the player, its grabber's numbers kept up to date
    GrabbedReport m_report;
    bool m_counterAsked = false;  // the duck's counter was asked for (record +0x14 = 0xe)
    anim::Vec3 m_duckAttacker;    // where the attacker that made the player duck stood
    std::uint32_t m_clipSeen = 0; // the clip playing at the end of the last update, and its time
    float m_clipTimeSeen = 0.0F;
    int m_duckCounters = 0;
    combat::CombatRandom m_grabbedRandom;
    combat::RepeatTracker m_repeat; // halves the rage of a long run of one kind; the throw bonus
    int m_hitsTaken = 0;
    int m_hitsBlocked = 0;
    int m_hitsDucked = 0;
    int m_hitsArmoured = 0;
};

} // namespace coney::human
