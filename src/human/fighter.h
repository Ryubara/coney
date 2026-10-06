// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "characters/anim_set.h"
#include "combat/anim_ranges.h"
#include "combat/being_hit.h"
#include "combat/commands.h"
#include "combat/grabbed.h"
#include "combat/meters.h"
#include "combat/player_combat.h"
#include "combat/power_class.h"
#include "combat/rage_awards.h"
#include "human/combatant.h"
#include "human/holdable.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
#include "human/locomotion.h"
#include "human/turn_and_slide.h"
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
/// The object search's second, wider pass: objects within this many degrees of the heading.
inline constexpr float kObjectWideConeDegrees = 135.0F;
/// The strong grapple's search range without an Anim Range List (Rembrandt's, **Coney's choice**).
inline constexpr float kStrongGrappleFallbackRange = 3.0F;
/// Beyond the attack's far range the attacker only turns, at most this much (**Coney's reading** of the research's
/// "capped at 8": degrees).
inline constexpr float kAttackTurnCapDegrees = 8.0F;
/// The player's health (Rembrandt's 900 at runtime, record `+0x146`).
inline constexpr int kPlayerHealth = 900;
/// A held human frees itself after this many of its own updates without its grabber keeping the hold
/// (Fighter::keepHold()). **Coney's choice**: the humans act in an order that alternates each step, so a live grabber
/// keeps the hold at most 2 of the victim's updates apart; 3 means it has stopped.
inline constexpr int kHoldLostUpdates = 3;

/// A breakable object square may strike (a glass pane): its handle and the point an attack aims at.
struct ObjectTarget {
    double handle = 0;
    anim::Vec3 point{}; ///< The pane's centre.
};

/// What the fighter is given each update.
struct FighterInput {
    combat::CommandId command = combat::command::kNone; ///< From the command matcher.
    std::uint16_t buttons = 0;                          ///< The held buttons.
    combat::Stick stick;                                ///< The camera-turned stick in the facing frame.
    combat::Stick padStick;                             ///< The stick as the pad reads it (the minigames').
    Gait gait = Gait::Standing;
    anim::Vec3 position;                     ///< The player's feet.
    float heading = 0.0F;                    ///< The player's heading, radians.
    std::uint64_t nowMs = 0;                 ///< Game time, whole milliseconds.
    std::span<Combatant* const> targets;     ///< The humans that can be fought.
    float stepSeconds = kStepSeconds;        ///< The characters' step: 1/30 s, less in slow motion.
    std::span<const ObjectTarget> objects{}; ///< The breakable objects square may strike.
};

/// The camera shake a reaction asks for (docs/research/camera.md#shake): on the attacker's camera when a player hit,
/// and from level 2 on the victim's when it is a player.
struct ReactionShake {
    int level = 0;                 ///< The hit code's strength bits, 0-3 (0 stops a shake).
    bool attackerIsPlayer = false; ///< A player's hit.
};

/// What kind of fighter a human is: the rules that differ between a player and a human no player controls.
struct FighterProfile {
    /// A player (human `+0x1b0` not -1): its hits ignore the victim's hit armour; as a victim it has the human flags
    /// `0x400` (combo hits keep their strength) and `0x20000000000` (the health floor), and its own hit armour while it
    /// winds up. **Coney choice**: a human no player controls has none of those flags (its flags are not researched).
    bool player = true;
    /// Its power class (the player's, class 64, unless given).
    combat::PowerClass powerClass = combat::kPlayerPowerClass;
    /// Its health's maximum (record `+0x146`).
    int health = 0;
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
// The fields stay grouped by the mechanic they serve (grab, hold, hits, reactions) rather than by size: there is one
// Fighter per human, so the few dozen padding bytes cost nothing worth the lost readability.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
class Fighter {
  public:
    /// A fighter whose damage comes from `ranges` (may be null: no damage), coin flips seeded with `seed`, of `profile`
    /// (the player's power class and kPlayerHealth by default; a profile's health of 0 takes kPlayerHealth).
    explicit Fighter(const combat::AnimRangeList* ranges, std::uint32_t seed = 1, const FighterProfile& profile = {});

    /// One update: the hits, warnings and grab taken since the last update (the victim's side), the timers of a
    /// reaction, then a grab's alignment turns and its pair's moments (the connect, the snap at a clip's end), then
    /// combat decides, the fighter plays the clips, turns `heading` to a target it attacks, grabs or tackles, lands the
    /// hits, keeps or drops its target, and puts an attached victim at its offset.
    void update(const FighterInput& input, HumanAnimator& animator, float& heading);

    /// Whether combat's states move the body this update rather than the stick: blocking, holding someone or held,
    /// mugging, a theft, or reacting to a hit (stunned, down, getting up). These stand for the state word's part of
    /// the original's busy test (`0x00223cb0`); a move's clip holds the stick through the record's `+0x08` and the
    /// locomotion gate (human/locomotion_gate.h).
    [[nodiscard]] bool holdsMovement(const HumanAnimator& animator) const;
    [[nodiscard]] bool blocking() const { return m_combat.blocking(); }
    [[nodiscard]] const combat::PlayerCombat& combat() const { return m_combat; }
    [[nodiscard]] combat::PlayerCombat& combat() { return m_combat; }
    /// The output of the last update.
    [[nodiscard]] const combat::CombatOutput& last() const { return m_last; }
    /// The victim held in a grab, a tackle or a mugging; null when none.
    [[nodiscard]] const Holdable* held() const { return m_held; }
    /// The hold is from the victim's rear (a grab from behind, after a spin or in the mugging).
    [[nodiscard]] bool fromRear() const { return m_rear; }
    /// How the grab's two bodies are held together now.
    [[nodiscard]] PairStage pairStage() const { return m_pair; }
    /// The velocity (m/s, horizontal) a grab's alignment slides the body at this update, counting the slide down; zero
    /// when none. The human calls it once per update while combat holds the movement.
    [[nodiscard]] anim::Vec3 takeSlide();
    /// One state update's step of `dt` of an attack's steer onto its target (TurnAndSlide::step()): the turn to add to
    /// the heading and the slide's velocity, on top of the clip's root motion. The human calls it once per update.
    [[nodiscard]] TurnAndSlideStep takeSteer(float dt) { return m_steer.step(dt); }
    /// The attack's steer still under way.
    [[nodiscard]] const TurnAndSlide& steering() const { return m_steer; }
    /// The target human the search would pick for an attack of `range` metres (null for none).
    /// @orig 0x0027a6c0 Player_PickTarget (unknown)
    [[nodiscard]] static Combatant* pickTarget(const FighterInput& input, float range);
    // Square's object target, with no human in front: of `input.objects` within the object attack's far range, the
    // nearest within 54° of the stick's heading (the facing at rest), else within 135°. Null for none.
    // **Coney choices**: the range is the object attack's far range (the original approaches from farther, not built);
    // the 135° pass keeps the whole range (the original's × 0.8 is not applied).
    [[nodiscard]] const ObjectTarget* pickObjectTarget(const FighterInput& input) const;
    // Circle + cross's search (combat::nearestInCone() along the stick, combat::strongGrappleRange()): makes the human
    // found the target and returns it when it may be grabbed, else null.
    Holdable* strongGrappleTarget(const FighterInput& input);
    /// The snap's target: the nearest human within combat::kSnapSearchRange, combat::kSnapSearchCone of the stick's
    /// direction and combat::kSnapSearchHeight, standing, with health left and targetable (null for none).
    /// **Coney stand-in**: the clear line to it (`0x00222a90`) is not tested.
    /// The original's search is `0x0027aa38(h, 0x80)` (docs/research/combat.md#attacks).
    [[nodiscard]] static Combatant* snapTarget(const FighterInput& input);
    /// Hits that reached a target, and the damage they did.
    [[nodiscard]] int hitsLanded() const { return m_hitsLanded; }
    [[nodiscard]] int damageDealt() const { return m_damageDealt; }
    /// The anim ids of the hits this player's human struck a victim with in its last update (landed or met by a
    /// block alike), oldest first: what the combat tutorial's callback hears (docs/research/hud.md#tutorial-callback).
    /// An AI's hits are not kept.
    [[nodiscard]] const std::vector<int>& strikes() const { return m_strikes; }
    /// The object an object attack's hit struck this update (its handle); nothing otherwise.
    [[nodiscard]] std::optional<double> objectHit() const { return m_objectHit; }
    /// Starts the stereo theft (mode 3) at game time `nowMs`, a stage `stageTurns` turns of the stick: 683
    /// `STEREO_STEAL_INTRO`, then the loop 684 while the game runs (docs/research/combat.md#stereo-theft).
    /// @orig 0x0022dd98 StereoTheft_Start (unknown)
    void startStereoTheft(HumanAnimator& animator, std::uint64_t nowMs, float stageTurns);
    /// The held object's anim set from now on (0 for none), which square and cross branch on (combat::armedAttack()).
    void setAnimSet(int set) { m_animSet = set; }
    [[nodiscard]] int animSet() const { return m_animSet; }

    /// The target kept (human `+0xc8`); null when none.
    [[nodiscard]] const Combatant* target() const { return m_target; }
    [[nodiscard]] Combatant* target() { return m_target; }
    /// The target the fight stance is locked onto (combat::lockedOn()): it has one and the lock-on settings lock; null
    /// otherwise. Locked, the human faces it every update and walks in the combat walk.
    [[nodiscard]] const Combatant* lockTarget() const;
    /// Whether it fights as a player (FighterProfile::player).
    [[nodiscard]] bool player() const { return m_player; }

    // The human flag word (human `+0xe0`, human/human_flags.h): what the scripts switch on a human and combat reads.

    /// The flags; a player's start as flag::kPlayerFlags, any other human's as 0.
    [[nodiscard]] std::uint64_t flags() const { return m_flags; }
    [[nodiscard]] bool hasFlag(std::uint64_t bit) const { return (m_flags & bit) != 0; }
    /// Replaces the flags.
    void setFlags(std::uint64_t flags) { m_flags = flags; }
    /// Sets (`on`) or clears the bits of `bits`.
    void setFlag(std::uint64_t bits, bool on) { m_flags = on ? (m_flags | bits) : (m_flags & ~bits); }

    // What the scripts do to a fighter (docs/references/bindings/character.md).

    /// `HuRevive`: back on its feet with full health; a stun, the ground and a reaction end, the idle plays.
    /// @orig 0x002377f8 Human_Revive (unknown)
    void revive(HumanAnimator& animator);
    /// `HuSetNormalMode`: a grab it holds or is held in ends, a stun ends, rage ends; with `full` also a human out of
    /// health or down gets back up with at least 1 health and the idle plays. **Coney's reading** of the summary
    /// (docs/references/bindings/character.md#husetnormalmode): fire, wounds, slow motion and held objects are not in
    /// Coney, and the state machine's restart is the idle.
    /// @orig 0x0023a210 Human_SetNormalMode (unknown)
    void setNormal(HumanAnimator& animator, bool full);
    /// Breaks any pair this human is in from outside, as every placement of it does (a teleport, a scene's end
    /// placement): it plays no clip and is free at once; a victim it holds is unlinked and plays its reaction (145
    /// from a front grab, 107 from the rear or a mugging, 245 then the rise 199 from the mount) and stands free; a
    /// grab holding it ends, its grabber playing its own reaction on its next update (pairBroken()). Not the grab's
    /// release: 95 / 94 never play.
    /// @orig 0x00258a88 Human_BreakPair (unknown)
    void breakPair();
    /// Whether this human is in a pair: holding, held or mounted, or held in another human's grab. A scene's start
    /// leaves such a human out (docs/research/scenes.md#humans, `0x002263d8`).
    [[nodiscard]] bool inPair() const;
    /// The hold this human was in when a placement broke it (breakPair()), once: its grabber reads it on its next
    /// update to play its side's reaction; nothing otherwise.
    [[nodiscard]] std::optional<TargetState> takeBrokenHold() { return std::exchange(m_brokenFrom, std::nullopt); }
    /// Puts back a broken hold taken from the fighter this one replaces (a placement starts the fighting afresh, but
    /// the grabber must still hear of the break).
    void setBrokenHold(std::optional<TargetState> broken) { m_brokenFrom = broken; }
    /// The grabbing player's movement: with the hold standing still (no move playing) and the stick beyond
    /// CombatTuning::grabTurnStick, turns `heading` towards the stick's heading `stickHeading` + 180° (the grabber's
    /// back to the stick) by combat::grabTurnStep() and returns the pair's backward walk (m/s, world axes); zero
    /// otherwise.
    /// @orig 0x00245310 Human_MoveGrabbing (unknown)
    [[nodiscard]] anim::Vec3 moveGrab(float stickHeading, float stickMagnitude, const HumanAnimator& animator,
                                      float& heading);

    // The victim side: the entry points an attacking human and the tests use.

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
    /// Another human's grab or tackle that drives this one (the player holding an AI's human, Holdable) puts it in
    /// `targetState`: Held or Mounted holds it (what it was doing ends; it neither moves itself nor acts until let go),
    /// Standing frees it, Grounded knocks it down to rise later; a stun ends. The clips are the caller's.
    void enterHold(TargetState targetState, HumanAnimator& animator, std::uint64_t nowMs);
    /// Its grabber still drives the hold this update (Holdable::keepHold()). A held human whose grabber has not kept
    /// the hold for kHoldLostUpdates of its own updates is let go (freeFromLostGrabber()).
    void keepHold() { m_holdUnkept = 0; }
    /// Placed by its grabber each update (Holdable::setAttached()); only while held.
    void setHoldAttached(bool attached) { m_holdAttached = attached && m_holdState.has_value(); }
    [[nodiscard]] bool holdAttached() const { return m_holdAttached; }
    /// Held or Mounted while a grab or a tackle another human drives holds it (enterHold()); none otherwise.
    [[nodiscard]] std::optional<TargetState> holdState() const { return m_holdState; }
    /// What the player did to its grabber in the last update.
    [[nodiscard]] const GrabbedReport& grabbedReport() const { return m_report; }
    /// The shake the reaction played in the last update asks for; none when it played none.
    [[nodiscard]] const std::optional<ReactionShake>& reactionShake() const { return m_reactionShake; }
    /// Whether rage started in the last update (a level-1 shake on the player's camera).
    [[nodiscard]] bool rageStarted() const { return m_rageStarted; }
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
    /// Whether a hit plays no reaction (human `+0xe0` bit `0x800`): the hit still takes its health, but the human's
    /// clip goes on. The AI's block goal sets it for its first updates (docs/research/ai.md#block).
    [[nodiscard]] bool hitReactionsOff() const { return m_hitReactionsOff; }
    void setHitReactionsOff(bool off) { m_hitReactionsOff = off; }

  private:
    // --- fighter.cpp: the update, the attacks and the targets.

    // The nearest standing (or grounded, with `grounded`) target within `range` in front of the player: within 90° of
    // its facing turned by `offset` radians (a snap's side, snapOffset()).
    [[nodiscard]] Combatant* inFront(const FighterInput& input, float range, bool grounded, float offset = 0.0F) const;
    // For a snap `animId`, the angle from the facing (radians, positive to the left, as headings) towards the other
    // human: the Anim Range List's direction (`+0x00`, `+0x02`), or without one its side (right, left or back); 0 for
    // any other attack.
    [[nodiscard]] float snapOffset(int animId) const;
    // The strike reach of attack `animId`.
    [[nodiscard]] float reachOf(int animId) const;
    // The dispatcher's input from the world: square's target, circle's search, the hold, and the record's +0x08.
    [[nodiscard]] combat::CombatInput combatInput(const FighterInput& input, const HumanAnimator& animator,
                                                  bool helpless);
    // Plays what the dispatcher decided: the block, rage, a grab or tackle, the grab's moves, the mugging, a theft,
    // an attack.
    void playDecisions(const combat::CombatOutput& out, combat::CombatMode before, const FighterInput& input,
                       HumanAnimator& animator, float& heading);
    // Plays an attack the dispatcher started (`animId`), with what follows it, turning and sliding to its target.
    void playAttack(int animId, const FighterInput& input, HumanAnimator& animator, float& heading);
    // Turns (and within the far range slides) towards the target of attack `animId`, as Attack_Start steers: within
    // the far range the turn and the slide onto the reach are spread at a constant rate over the time to the clip's
    // first event, from the next state update (m_steer); beyond it the facing turns at once, capped.
    // @orig 0x002761c8 Attack_SteerToTarget (unknown)
    // @orig 0x00276008 Attack_TurnToTarget (unknown)
    void steer(int animId, const FighterInput& input, const HumanAnimator& animator, float& heading);
    // A snap's steer onto the target its search found (m_snapTarget): the turn and the slide onto the reach over
    // combat::kSnapSteerSeconds, from the next state update, when it is within the snap's far range; nothing beyond.
    // The original's snap (`0x00264460`) steers so (docs/research/combat.md#attacks).
    void steerSnap(int animId, const FighterInput& input, float heading);
    // Where an attack of clip `clipId` at `target`, `distance` away, should stand from it: the clip's reach, 0.07 m
    // longer for a big target and 0.1 m shorter from behind it; without a reach, where the target stands.
    [[nodiscard]] float steerReach(std::uint32_t clipId, const Combatant& target, const FighterInput& input,
                                   float distance) const;
    // The block's clip: the shuffle with the stick pushed, the sustain otherwise.
    static void playBlock(const FighterInput& input, HumanAnimator& animator);
    // Lands a hit of attack `animId` and `damage`, and gives the rage it earns.
    void landHit(int animId, int damage, const FighterInput& input);
    // Gives the rage hit `animId` earns (`isThrow`: the throw bonus), only to a human that may rage (flag 0x2000000,
    // `Human_AddRage`).
    void earnRage(int animId, std::uint64_t nowMs, bool isThrow = false);
    // Remembers the clip playing at the end of the update and its time (the pair's moments, the duck's window).
    void noteClip(const HumanAnimator& animator);
    // Keeps the target or drops it (gone, out of health, down, or too far without L1 or a hold), and lets L1 pick one.
    void trackTarget(const FighterInput& input);

    // --- fighter_grab.cpp: a grab the player holds.

    // The victim held is gone from the targets (removed from the level), or something other than this grab freed it
    // (a script's normal mode, a hit that knocked it down, a placement that broke the pair): the hold ends without
    // touching a victim that is gone; after a broken pair this side plays its reaction (138 or 106, or 244 from the
    // mount; docs/research/combat.md#pair-break).
    void dropLostHold(const FighterInput& input, HumanAnimator& animator);
    // A grab or tackle started on `victim`: the intro plays (the player turning to face it), the victim waits. A grab
    // connects with front clip `connect` (72, or a strong grapple's strike), the rear one 2 on.
    void startHold(Holdable& victim, const FighterInput& input, float& heading, bool tackle, int connect,
                   HumanAnimator& animator);
    // The grabber's connecting clip for the side the grab is on; the victim plays the next id.
    [[nodiscard]] std::uint32_t connectClip() const { return static_cast<std::uint32_t>(m_connect + (m_rear ? 2 : 0)); }
    // A strong grapple's connect has ended in the hold: the victim takes the strike's damage, reported as the hold's
    // id (docs/research/combat.md#strong-grapple).
    void landGrapple(const FighterInput& input);
    // A power move in a grab (57, 63 and their extensions) has played out: the grab ends, the grabber settling through
    // 389 as queued, the victim on the ground, to get up after its time (docs/research/combat.md#grabbing).
    void endPowerMove();
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
    // Plays the mount's move the dispatcher decided: a strike and the victim's reaction (the next id), back to the
    // mount; the pick-up to the front hold; or getting off, the victim rising.
    void playMountAction(const combat::CombatOutput& out, HumanAnimator& animator);
    // The grab's mount has played (210 started): the victim is placed at the mount's point, as after a tackle.
    void seatMount(const FighterInput& input, const HumanAnimator& animator, float heading);
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
    // Held or mounted, but the grabber has stopped driving the hold (removed, out of the update): the human frees
    // itself with the victim's side of a broken pair (docs/research/combat.md#pair-break) rather than stay in the held
    // loop. **Coney's choice**: in the original every way a grabber leaves breaks the pair; Coney's grabber may stop
    // updating without that, so the victim's side makes sure.
    void freeFromLostGrabber(HumanAnimator& animator);

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
    std::uint64_t m_flags = 0; // the human flag word (+0xe0)
    combat::PlayerCombat m_combat;
    combat::CombatOutput m_last;
    Holdable* m_held = nullptr;
    Holdable* m_thrown = nullptr;      // the victim of the throw whose hit has not landed yet
    Holdable* m_candidate = nullptr;   // what the grab or tackle search found this update
    Combatant* m_target = nullptr;     // the target kept (human +0xc8)
    Combatant* m_snapTarget = nullptr; // what the snap's search found for this update's square (not the target)
    int m_connect = 72;            // the grab's front connecting clip: 72, or the strong grapple's 657 (649 in rage)
    anim::Vec3 m_slide;            // a grab's alignment's slide velocity, m/s
    int m_slideUpdates = 0;        // updates of slide left
    TurnAndSlide m_steer;          // an attack start's turn and slide onto its target
    anim::Vec3 m_holdOffset;       // the attached victim's place in the grabber's frame
    float m_holdTurn = 0.0F;       // the attached victim's heading less the grabber's
    std::uint32_t m_lastClip = 0;  // the grabber's clip at the end of the last update
    float m_turnStep = 0.0F;       // the grabber's alignment turn per update, radians
    float m_victimTurnStep = 0.0F; // the victim's
    int m_turnUpdates = 0;         // updates of turn left
    float m_grabTurn = 0.0F;       // the grab's stick turn of the last update, radians
    int m_duckCounters = 0;        // the duck counters played
    int m_hitsLanded = 0;
    int m_animSet = 0;                    // the anim set a held weapon applied (record +0x10's top)
    std::vector<int> m_strikes;           // a player's struck hits' anim ids in the last update
    std::optional<double> m_objectTarget; // the object square's object attack aims at
    std::optional<double> m_objectHit;    // the object an object attack struck this update
    int m_damageDealt = 0;

    // The victim side.
    combat::Health m_health;
    Victim m_victim;
    std::optional<AttackNotice> m_notice;
    std::optional<GrabCatch> m_catch;
    std::optional<GrabCatch> m_grabbed;      // the grab holding the player, its grabber's numbers kept up to date
    std::optional<TargetState> m_holdState;  // held or mounted by a grabber that drives it (enterHold())
    std::optional<TargetState> m_brokenFrom; // the hold a placement broke, for its grabber (takeBrokenHold())
    GrabbedReport m_report;
    std::optional<ReactionShake> m_reactionShake; // the last update's reaction's shake
    anim::Vec3 m_duckAttacker;                    // where the attacker that made the player duck stood
    std::uint32_t m_clipSeen = 0;                 // the clip playing at the end of the last update, and its time
    float m_clipTimeSeen = 0.0F;
    combat::CombatRandom m_grabbedRandom;
    combat::RepeatTracker m_repeat; // halves the rage of a long run of one kind; the throw bonus
    int m_hitsTaken = 0;
    int m_hitsBlocked = 0;
    int m_hitsDucked = 0;
    int m_hitsArmoured = 0;
    int m_holdUnkept = 0; // own updates held since the grabber last kept the hold (keepHold())
    // The flags, together so the class packs.
    bool m_player = true;         // FighterProfile::player
    bool m_l1Held = false;        // L1 held this update (record +0x00 0x8)
    bool m_tacklePending = false; // the tackle's intro plays; the victim reacts when its hit clip starts
    bool m_mountPending = false;  // the grab's mount (118) plays; the victim moves to the mount's point at 210
    bool m_mugOnTarget = false;
    bool m_rear = false; // the hold is from the victim's rear
    PairStage m_pair = PairStage::None;
    bool m_reacting = false;        // a hit's reaction (or an escape, a let-go) holds the player until it is over
    bool m_justCaught = false;      // the grab caught the player this update
    bool m_holdAttached = false;    // placed by that grabber each update
    bool m_rageStarted = false;     // rage started in the last update
    bool m_counterAsked = false;    // the duck's counter was asked for (record +0x14 = 0xe)
    bool m_hitReactionsOff = false; // human +0xe0 0x800
};

} // namespace coney::human
