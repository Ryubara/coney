// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "ai/action.h"
#include "ai/attack_kinds.h"
#include "ai/fight_book.h"
#include "ai/goal.h"
#include "ai/route_planner.h"
#include "ai/sectors.h"
#include "ai/steering.h"
#include "ai/targeting.h"
#include "combat/stick.h"
#include "human/human.h"

// A human's brain: what decides, for a human no pad drives, what it writes into its per-player record. It has a type
// (from the character class's behaviour), a reaction goal that has priority, a stack of ten goals and a queue of eight
// actions, a target with an attack slot on it, enemies, and the numbers its fighting is paced by. The brains run at the
// brains' place in the characters' step (ai::Brains), before every human's dispatcher, so what a brain writes is taken
// that same update as a pad's press would be.
// Research: docs/research/ai/index.md#brain, docs/research/ai/index.md#update-goals, docs/research/ai-fights.md#targets

namespace coney::raycast {
class CollisionMesh;
} // namespace coney::raycast

namespace coney::ai {

class Brain;
class Formation;
class Gang;
class ScriptServices;

/// A brain's type (`+0x04`), from its character class's behaviour byte (docs/research/ai/index.md#types).
enum class BrainType : std::uint8_t {
    Player = 0,     ///< The player: its update only keeps books.
    Cop = 1,        ///< Cops.
    Gang = 2,       ///< Gang soldiers and thugs; the sparring Warriors of `level99`.
    Warrior = 3,    ///< The Warriors (an ally): blocks a quarter as often.
    Civilian = 4,   ///< Civilians and bums.
    Dealer = 5,     ///< Dealers.
    CivilianDi = 6, ///< The `civl_co_di` kind.
};

/// Why a brain's target changed, as the original's `Brain_SetTarget` callers tell apart
/// (docs/research/gang-fights.md#set-target-causes), counted per brain for the all-humans trace (ai::FightTracer).
/// Coney's own bookkeeping: the original keeps no such count; its recordings log each call's return address.
enum class TargetCause : std::uint8_t {
    Other,     ///< Any other caller (scripts, a tactic's own answer to an event, a target that left the scene).
    Pick,      ///< The AI pick of the best enemy (`Brain_PickBestEnemy`, its clear and its set), Melee's own clear,
               ///< Spectate's end and FollowAndAttack's clear of a pick beyond 60 m of the leader.
    Front,     ///< The fight goal's forced re-target to the man in front (`FightGoal_Process`).
    HitBack,   ///< Hit back at the attacker (`GangBrain_OnEvent` event 1, directly or through `Brain_Fight`).
    Help,      ///< A help call answered (`Brain_OnViolenceSeen`, event 20) or TacticAttack's violence handler.
    Engage,    ///< The engage goal's re-target (`EngageEnemyGoal_Process`).
    Displaced, ///< Lost to a closer attacker taking the last attack slot (`Brain_ClaimAttackSlot`).
    Reaction,  ///< Its own state (stunned, wounded, knocked out, down) or a grab's or mount's partner.
    Order,     ///< A tactic starting, the reset (event 2), going for an item or the wreck's swing (`Human_DropTarget`
               ///< from `Tactic_Start`, `GetItemGoal_Start`, `WarriorVandalStealGoal_TryVandalise`). The original's
               ///< drop at Melee's end counts here too; Coney's keeps the target.
};
/// The number of TargetCause values.
inline constexpr std::size_t kTargetCauseCount = 9;
static_assert(static_cast<std::size_t>(TargetCause::Order) + 1 == kTargetCauseCount,
              "kTargetCauseCount must follow TargetCause's last value");
/// A TargetCause's name in the trace (`other`, `pick`, `front`, `hit_back`, `help`, `engage`, `displaced`,
/// `reaction`, `order`).
[[nodiscard]] const char* targetCauseName(TargetCause cause);

/// The brain type of behaviour byte `behaviour` (`CfgChar`'s second argument, class `+0x11a`); a value outside 0-6
/// gives Gang (**Coney choice**).
[[nodiscard]] BrainType brainTypeOf(int behaviour);

/// The human flags a human of brain type `type` is made with (`Human_Init`, docs/research/combat/index.md#human-flags):
/// the player 0x2000000, 0x2 and 0x4; a Warrior 0x2 and 0x4 (revivable, so any Warrior downed in a fight can be
/// revived); a cop and a dealer 0x2000 and 0x40000; any other none of these. **Coney's stand-in**: the others' 0x8
/// (woundable) on a 1-in-2 roll is not given, as no Coney code reads it and the roll would shift every seeded run.
/// @orig 0x00218008 Human_Init (unknown)
[[nodiscard]] std::uint64_t startingFlags(BrainType type);

/// The flags `flags` of a player handed back to the AI as `leftAs` (`Human_ReleasePlayer`): 0x2, 0x4 and 0x2000000
/// cleared, then 0x2 and 0x4 set again for brain type 0 or 3, 0x2000 for type 1; every other bit kept.
/// @orig 0x0022a2a8 Human_ReleasePlayer (unknown)
[[nodiscard]] std::uint64_t releasedFlags(std::uint64_t flags, BrainType leftAs);

/// The goal stack's size, the action queue's, the enemy list's and the attack slots' (`+0x40`, `+0x68`, `+0x164`,
/// `+0x1a4`).
inline constexpr std::size_t kGoalStackSize = 10;
inline constexpr std::size_t kActionQueueSize = 8;
inline constexpr std::size_t kMaxEnemies = 16;
inline constexpr std::size_t kMaxAttackSlots = 16;
/// A brain's defaults, the sparring Warriors' of `level99` as read at runtime (docs/research/ai/index.md#level99):
/// attackers allowed on one human at once (`+0x1e4`, `BrSetNumAttackSlots`), the melee ranges near and far (`+0x13c`,
/// `+0x140`, `BrSetMeleeRange`; a fight ends beyond the far range × 1.1), and the range (`+0x130`) and field of view
/// (`+0x12c`, a half-angle in radians) within which a brain counts an attack's start (event `0x10`), with a clear line
/// of sight.
inline constexpr std::size_t kDefaultAttackSlots = 4;
inline constexpr float kDefaultMeleeNear = 3.0F;
inline constexpr float kDefaultMeleeFar = 5.0F;
inline constexpr float kDefaultSightRange = 30.0F;
inline constexpr float kDefaultFieldOfView = 1.92F;
/// The threat response every brain is made with (`0x0028a570`, from `Human_Init`); 0 never fights.
inline constexpr int kDefaultThreatResponse = 2;
/// A brain thinks once every this many character steps (docs/research/ai/index.md#update).
inline constexpr std::uint64_t kThinkPeriod = 5;
/// `CfgAttackDelay`'s value for most kinds, ms (`config_preload2`, docs/research/ai-fights.md#attack-action).
inline constexpr int kDefaultAttackDelayMs = 200;
/// `CfgAttackDelay` by kind as `config_preload2` sets it, ms: the values the disc's scripts give, for a run without
/// them.
/// @coney-only the configuration scripts' values as defaults for a run without them
[[nodiscard]] constexpr std::array<int, kAttackKinds> referenceAttackDelays() {
    std::array<int, kAttackKinds> delays{};
    delays.fill(kDefaultAttackDelayMs);
    delays[12] = delays[13] = 400;
    delays[19] = delays[21] = 500;
    delays[20] = 1000;
    delays[32] = delays[33] = delays[34] = 300;
    delays[23] = delays[31] = delays[42] = 0;
    return delays;
}
/// The base chance to block (`CfgBaseChanceToBlock`, `0x00510ac8`, percent), 60 at runtime.
inline constexpr int kDefaultBaseBlockChance = 60;

/// The events Coney delivers to a human (`Human_OnEvent`, docs/research/ai/index.md#events): damage taken (1, a **Coney
/// stand-in** sender, Brains::reportDamage()), an enemy added to its brain
/// (`0xb`, for the gang's tactic), an attack started on it within its brain's range and field of view (`0x10`), and its
/// health run out (18, for its gang's message handler).
inline constexpr int kEventDamaged = 1;
inline constexpr int kEventReset = 2;
inline constexpr int kEventEnemyAdded = 0x0b;
/// The goals ran out (`0xd`): sent by a pop that empties the stack, about the human himself (Brain::popGoal()).
inline constexpr int kEventGoalsRanOut = 0x0d;
inline constexpr int kEventAttackWarning = 0x10;
inline constexpr int kEventViolence = 0x14;
inline constexpr int kEventDown = 18;
/// Put on the ground (`0x15`): sent to a man as he comes to lie there after a knockdown, a throw, a grab or a mount
/// (docs/research/gang-fights.md#waiting rule 5); his attackers gloat (ai::gloatOver()).
inline constexpr int kEventPutDown = 0x15;

/// One event to a human: its id, the other human it is about (null for none) and a value; for violence (`0x14`, a
/// help call among them) the victim who called (`+0x24`) and the new-enemy flag (`+0x0c`).
struct BrainEvent {
    int id = 0;
    Brain* other = nullptr;
    int value = 0;
    Brain* victim = nullptr;
    bool newEnemy = false;
};

/// The default hearing ranges, metres: noises (`+0x134`) and allies calling for help (`+0x138`)
/// (docs/references/bindings/gang.md#gangsethearrange).
inline constexpr float kDefaultHearRange = 50.0F;
inline constexpr float kDefaultHelpHearRange = 20.0F;

/// What the story scripts set on a brain's senses and reactions beyond its sight (docs/references/bindings/ai.md). Kept
/// for the brains' reactions, which Coney does not build yet (each field says who reads it).
struct BrainSenses {
    /// `BrSetInvestigateResponse` (`+0x224`): 0 does not go to look at disturbances. **Coney choice** until set: 1.
    int investigate = 1;
    /// `BrSetReactToViolence` (`+0x267`): reacts to fights it sees. **Coney choice** until set: true.
    bool reactsToViolence = true;
    /// `BrSetDamageResponse` (`+0x220`, 1 when a brain is made): no reader is on the page, so it is only kept.
    int damageResponse = 1;
    float hearRange = kDefaultHearRange;         ///< `+0x134`, `GangSetHearRange(gang, false, range)`.
    float helpHearRange = kDefaultHelpHearRange; ///< `+0x138`, `GangSetHearRange(gang, true, range)`.
    bool worldFlags = false;                     ///< `+0x2d1`, `GangCanUseWorldFlags`.
    int worldFlagPercent = 0;                    ///< `+0x2d2`.
    /// `BrSetPedType` (`+0x26c`, low 16 bits; 0 when made): 3 a civilian who stands up to attackers, 5 a rich mugging
    /// victim. **Coney stand-in**: the civilian brain's reactions and the mugging's money do not read it yet.
    std::uint16_t pedType = 0;
    /// `HuMarkReachable` (`+0x11e`): other AI may reach this human to fight it. Its readers (the reachable test
    /// `0x0028abc0`, the enemy scoring's 5-point cut, the attack picker and the combat goals) are not built yet.
    /// **Coney choice** until set: true (the value a new brain starts with is not traced).
    bool reachable = true;
};

/// The configuration a brain fights by: what the configuration scripts set (docs/research/ai-fights.md#fight).
struct FightSettings {
    AttackWeights attackWeights = attNormal(); ///< The class's `Att_*` table (`+0x298`).
    /// `CfgAttackDelay` by kind (the table at `0x006b6658`), ms.
    std::array<int, kAttackKinds> attackDelaysMs = referenceAttackDelays();
    int baseBlockChance = kDefaultBaseBlockChance; ///< `CfgBaseChanceToBlock`, percent.
    TargetingPoints targeting;  ///< `CfgSetTargetingPoints` and `CfgSetTargetingPointsEx`: the enemy score's weights.
    GangFightTable gangFight{}; ///< `CfgGang` by gang kind: the spacing, hand-over, tackle and rear-grab values.
};

/// One human's brain.
class Brain {
  public:
    /// The brain of `human` (not owned; it must outlive the brain) of `type`, fighting by `settings`, its rolls drawn
    /// from a generator seeded with `seed`, made at game time `nowMs` (its last update until it has one). Enabled, with
    /// no goals, no actions and no target.
    /// @coney-only object set-up; it hands the human its brain's type, which the original reads in place
    Brain(human::Human& human, BrainType type, const FightSettings& settings, std::uint32_t seed,
          std::uint64_t nowMs = 0);
    Brain(const Brain&) = delete;
    Brain& operator=(const Brain&) = delete;
    Brain(Brain&&) = delete;
    Brain& operator=(Brain&&) = delete;
    ~Brain();

    /// Its human, and its type.
    [[nodiscard]] human::Human& human() { return *m_human; }
    [[nodiscard]] const human::Human& human() const { return *m_human; }
    /// Its type (`+0x04`).
    [[nodiscard]] BrainType type() const { return m_type; }
    /// `HuSwitchPlayer`: the brain becomes a player's (BrainType::Player) or, with another `type`, an AI brain of that
    /// type that has lost the pad (its pad hook dropped). **Coney's**: the
    /// original marks the player on the human (`+0x380`) and the player record; Coney's brain type is that mark.
    /// Research: docs/research/rumble.md#switch-player
    void setType(BrainType type);
    /// Enabled (`+0x08`): Brains skips a brain that is not.
    [[nodiscard]] bool enabled() const { return m_enabled; }
    /// Sets the enabled byte.
    void setEnabled(bool enabled) { m_enabled = enabled; }

    // --- The scripts' hold (docs/research/ai-scripting.md#scripted, docs/research/ai/index.md#handlers).

    /// Whether a thug goes for the weapons lying in the level (`+0x265`, `BrSetThugWantsWeapon`). **Coney stand-in**:
    /// Coney has no weapons lying about yet, so it is only kept. True until set (**Coney choice**: the scripts turn it
    /// off in their set-up).
    [[nodiscard]] bool wantsWeapon() const { return m_wantsWeapon; }
    /// Sets whether a thug goes for weapons.
    void setWantsWeapon(bool wants) { m_wantsWeapon = wants; }
    /// The script handle of its human (human vtable `+0x2c`): what its goals' callbacks and its gang's handlers pass;
    /// 0 (Coney's `NilHandle`) until the scripts name it.
    [[nodiscard]] double handle() const { return m_handle; }
    /// Sets the script handle.
    void setHandle(double handle) { m_handle = handle; }
    /// Its human's character class (`HuCreate`'s type, which `GoalDealer` reads); -1 when not known. Setting it gives
    /// it to the human too (human `+0xcc`, which a Warrior's damage reads).
    [[nodiscard]] int characterClass() const { return m_characterClass; }
    /// @coney-only setter that gives the class to the human too
    void setCharacterClass(int characterClass);
    /// "Dead" to the AI (`+0x09`, `BrDead`): the actions are cleared (the goals stay) and the handlers installed
    /// again. A dead brain runs only the goals and actions it is given: its think and its event handler do nothing,
    /// so it counts no attack warnings, and a dead player's brain runs them too. A player's brain set dead gives up the
    /// pad and set alive takes it back (per-player `+0x1b`), through the pad control hook.
    /// @orig 0x00292330 Brain_SetDead (unknown)
    /// @orig 0x0028c1a8 Brain_InstallHandlers (unknown)
    void setDead(bool dead);
    /// The knock-out (`Brain_OnKnockedOut`, from `Human_KnockOut` when his health runs out): the reaction goal ended,
    /// every goal popped, the actions cleared, the target dropped (his attack slot and active place on it released)
    /// and the enemies cleared; the brain then stops (enabled byte `+0x08` cleared) until wakeUp(). Returns the target
    /// he had (the event `0x12`'s foe).
    /// @orig 0x0028c3b0 Brain_OnKnockedOut (unknown)
    Brain* knockOut();
    /// Back up (`Human_WakeUp` → `Brain_EnableAndResume`): the brain runs again.
    void wakeUp() { m_knockedOut = false; }
    /// Whether he is knocked out (the brain stopped).
    [[nodiscard]] bool knockedOut() const { return m_knockedOut; }
    /// Dead to the AI (`+0x09`).
    [[nodiscard]] bool dead() const { return m_dead; }
    /// What a player's brain calls when it gives up the pad (false) or takes it back (true).
    using PadControl = std::function<void(bool padControlled)>;
    /// Sets what a player's brain calls when it gives up or takes back the pad.
    void setPadControl(PadControl control) { m_padControl = std::move(control); }
    /// Suspended (`+0x0a`, `BrSuspend`): its update keeps only the time.
    [[nodiscard]] bool suspended() const { return m_suspended; }
    /// Sets the suspended byte.
    void setSuspended(bool suspended) { m_suspended = suspended; }
    /// Its human's gang (`+0x20c`); null for none. ai::Gangs sets it.
    [[nodiscard]] Gang* gang() const { return m_gang; }
    /// Sets its gang (null for none).
    void setGang(Gang* gang) { m_gang = gang; }
    /// The formation it follows in (`+0x212`); null for none. ai::Formation sets it.
    [[nodiscard]] Formation* following() const { return m_following; }
    /// Sets the formation it follows in (null for none).
    void setFollowing(Formation* formation) { m_following = formation; }
    /// Its human's event once the gang has passed on it (handler C of `Brain_OnEvent`): nothing for a dead brain, else
    /// its type's handler and the shared one (answerEvent(), docs/research/ai-brain-types.md). Returns
    /// whether it was used.
    /// @orig 0x0028f928 Brain_OnEvent (unknown)
    bool onEvent(const BrainEvent& event);
    /// Whether its human's health running out has been told (event 18).
    [[nodiscard]] bool downReported() const { return m_downReported; }
    /// Sets whether the health running out has been told.
    void setDownReported(bool reported) { m_downReported = reported; }
    /// Whether its human's lying on the ground has been told (event kEventPutDown) since he last stood.
    [[nodiscard]] bool lyingReported() const { return m_lyingReported; }
    /// Sets whether the lying on the ground has been told.
    void setLyingReported(bool reported) { m_lyingReported = reported; }
    /// Its human's health when the brains last looked (Brains::reportDamage()); -1 before the first look.
    [[nodiscard]] int seenHealth() const { return m_seenHealth; }
    /// Sets the health seen at the last look.
    void setSeenHealth(int health) { m_seenHealth = health; }
    /// Health its human lost before this step's tactics ran, not yet reported as event 1 (Brains::noteDamage()).
    [[nodiscard]] int unreportedDamage() const { return m_unreportedDamage; }
    /// Sets the health lost and not yet reported.
    void setUnreportedDamage(int damage) { m_unreportedDamage = damage; }
    /// What its human's own script handlers are reached through (deliverEvent()); null for none. The level's scripted
    /// brains set it when they bind the brain to a handle.
    [[nodiscard]] ScriptServices* services() const { return m_services; }
    /// Sets what its human's script handlers are reached through.
    void setServices(ScriptServices* services) { m_services = services; }

    // --- The update (docs/research/ai/index.md#update-goals).

    /// One update at game time `nowMs`: for a type-0 brain (the player's) only its books, unless it is dead; for every
    /// other type, and a dead player's, Brain_UpdateGoals: the move cleared first (stopMove()), then skipped while the
    /// human is airborne, out of health, its gang is suspended or the brain is; the enemy scan when its schedule allows
    /// (Gangs::scans(), ScanSchedule::maybeScan(); a brain with no gang has none); the reaction goal when the human's
    /// state calls for one, else the goal stack; then the actions. The human's attack announcements within the range
    /// and field of view are delivered at the start as events `0x10` (deliverEvent()), which count the warnings
    /// (`+0x200`); the count is cleared at the end.
    /// @orig 0x0028f8b8 Brain_Update (unknown)
    /// @orig 0x0028fbb0 Brain_UpdateGoals (unknown)
    void update(std::uint64_t nowMs);
    /// One think (handler B), one update in five; nothing for a dead brain, whose handler B is a stub. **Coney
    /// choice**: the types' think handlers are not traced, so a think only counts; a fight is started by its caller
    /// (GoalFight, ai::AiHumans).
    /// @orig 0x0028f6c0 Brain_Think (unknown)
    void think(std::uint64_t nowMs);
    /// Updates and thinks so far (`+0x34`, `+0x38`).
    [[nodiscard]] std::uint64_t updates() const { return m_updates; }
    /// Thinks so far (`+0x38`).
    [[nodiscard]] std::uint64_t thinks() const { return m_thinks; }
    /// The game time of the update under way (or the last), ms (`+0x30`).
    [[nodiscard]] std::uint64_t nowMs() const { return m_nowMs; }

    // --- The goal stack.

    /// Pushes `goal`: the old top is suspended, the actions are cleared (but an action that refuses to abort) and
    /// `goal` becomes the top, started once the queue is empty. Refused (false, `goal` dropped) when the stack is full.
    /// @orig 0x0028d758 Brain_PushGoal (unknown)
    /// @orig 0x0029eea0 Goal_Suspend (unknown)
    bool pushGoal(std::unique_ptr<Goal> goal);
    /// Takes the top goal off the stack, ends it when it had started and frees it, then resumes the new top at once
    /// (started, when it never was) if the action queue is empty, else before its next process. When the stack is
    /// then empty the human's goals ran out: he is sent event kEventGoalsRanOut about himself (deliverEvent()), which
    /// his script handlers and his gang's may answer, and it is counted (goalsRanOut()). Nothing on an empty stack
    /// (**Coney's guard**: the original is never called on one).
    /// @orig 0x0028d7d8 Brain_PopGoal (unknown)
    void popGoal();
    /// Pops every goal, then clears the goal base (−1).
    /// @orig 0x0028d910 Brain_ClearGoals (unknown)
    void clearGoals();
    /// Marks the goal base (`Brain_MarkGoalBase`, `+0x2d`): the top's index becomes the base, so the goals now on the
    /// stack stay under what a tactic pushes. Nothing on an empty stack (the base stays as it was).
    /// @orig 0x0028d8a0 Brain_MarkGoalBase (unknown)
    void markGoalBase();
    /// Pops down to the goal base (`Brain_PopToGoalBase`): every goal above it, then the base is cleared (−1, which
    /// pops everything next time).
    /// @orig 0x0028d8c0 Brain_PopToGoalBase (unknown)
    void popToGoalBase();
    /// A tactic's goal for this member: pops to the base, marks it again and pushes `goal` on top, so the member's own
    /// goals stay underneath (docs/research/ai-gangs.md#tactics). False when the stack is full.
    bool pushTacticGoal(std::unique_ptr<Goal> goal);
    /// The goal base's index (−1: none).
    [[nodiscard]] int goalBase() const { return m_goalBase; }
    /// The goal of `type` above the goal base, nearest the top; null when none.
    [[nodiscard]] Goal* findGoalAboveBase(GoalType type);
    /// The goal of `type` on the stack, nearest the top; null when none.
    /// @orig 0x0028d960 Brain_FindGoal (unknown)
    [[nodiscard]] Goal* findGoal(GoalType type);
    /// Whether a Melee (8) or LeftTurf (`0x3f`) goal is on the stack: despite the original's name, the fight goal
    /// (`0xf`) is not tested. Research: docs/research/ai-reimpl.md#unit-2
    /// @orig 0x00290348 Brain_HasFightGoal (unknown)
    [[nodiscard]] bool hasFightGoal();
    /// The top goal; null when the stack is empty.
    [[nodiscard]] Goal* topGoal() { return m_goals.empty() ? nullptr : m_goals.back().get(); }
    [[nodiscard]] const Goal* topGoal() const { return m_goals.empty() ? nullptr : m_goals.back().get(); }
    /// How many goals are on the stack.
    [[nodiscard]] std::size_t goalCount() const { return m_goals.size(); }
    /// The goal `index` places above the bottom of the stack (0 is the bottom); null past the top.
    [[nodiscard]] const Goal* goalAt(std::size_t index) const {
        return index < m_goals.size() ? m_goals[index].get() : nullptr;
    }
    /// The reaction goal (`+0x3c`); null when none.
    [[nodiscard]] const Goal* reactionGoal() const { return m_reaction.get(); }
    /// Times the goals ran out (event `0xd`).
    [[nodiscard]] int goalsRanOut() const { return m_goalsRanOut; }
    /// Help calls its human made (broadcastHelpCall()) and HelpRespond goals sent to it (sendHelper()), counted for the
    /// all-humans trace (ai::FightTracer). Coney's own counts: the original keeps none; its recordings log the same
    /// calls with hooks (docs/research/rumble.md#gang-battle-metrics rule 11).
    [[nodiscard]] int helpCalls() const { return m_helpCalls; }
    /// Counts one help call its human made (broadcastHelpCall() calls it on entry, however many hear it).
    void countHelpCall() { ++m_helpCalls; }
    /// The HelpRespond goals sent to it so far (helpCalls() above).
    [[nodiscard]] int helpersSent() const { return m_helpersSent; }
    /// Counts one HelpRespond goal sent to it (sendHelper() calls it).
    void countHelperSent() { ++m_helpersSent; }

    // --- The action queue.

    /// Queues `action` at the back. Refused (false, `action` dropped) when the queue is full.
    /// @orig 0x0028d9f0 Brain_AllocAction (unknown)
    bool queueAction(std::unique_ptr<Action> action);
    /// Frees the front action.
    /// @orig 0x0028da60 Brain_PopAction (unknown)
    void popAction();
    /// The brain's part of a placement of its human (Human::place()): a front action that is a move (MoveAction, the
    /// original's `MoveTo`, type 1) is popped, aborted first when it has started, so the human does not walk on from
    /// where he stood (docs/research/teleports.md#placement). Every caller that places a human with a brain calls it
    /// after the placement.
    /// @orig 0x0023d440 Human_SetTransform (unknown)
    void placed();
    /// Frees the actions from the front, asking each that has started to stop first; stops at one that refuses (one
    /// never started is freed unasked). Returns whether the queue is empty.
    /// @orig 0x0028db20 Brain_ClearActions (unknown)
    bool clearActions();
    /// Brain `+0xcc` bit 1: a GiveWay action lives (asked to give way again, the brain answers yes at once).
    [[nodiscard]] bool givingWay() const { return m_givingWay; }
    /// Sets the GiveWay bit.
    void setGivingWay(bool on) { m_givingWay = on; }
    /// Brain `+0xcc` bit 4: inside a push-aside, which keeps its recursion from looping.
    [[nodiscard]] bool pushingAside() const { return m_pushingAside; }
    /// Sets the push-aside bit.
    void setPushingAside(bool on) { m_pushingAside = on; }
    /// Actions queued.
    [[nodiscard]] std::size_t actionCount() const { return m_actionCount; }
    /// The front action; null when the queue is empty.
    [[nodiscard]] Action* frontAction();
    /// The same, for a reader.
    [[nodiscard]] const Action* frontAction() const;
    /// Clears the goals, then the actions (`BrFlush`).
    /// @orig 0x00292530 Brain_FlushAll (unknown)
    void flush();

    // --- Fighting (docs/research/ai-fights.md#targets).

    /// `GoalFight(human, target)`: clears the actions, then fight() with no time limit (kNoFightLimit).
    /// @orig 0x002b2b90 Brain_StartFight (unknown)
    void startFight(Brain& target);
    /// Fights `target` when the threat response allows it: adds it to the enemies, takes it as the target (claiming an
    /// attack slot on it) and pushes the fight's goals (pushFightGoals()) for `durationMs` (kNoFightLimit for none).
    /// Returns false when the threat response is 0.
    /// @orig 0x0028d2e8 Brain_Fight (unknown)
    bool fight(Brain& target, int durationMs = kNoFightLimit);
    /// Pushes the fight's three goals (docs/research/ai-fights.md#targets): nothing while its gang has a tactic (the
    /// tactic fights) or for a human down or out of health; otherwise it pops any Melee (8) or FindEnemy (`0x41`) goal
    /// with every goal above it, then pushes FindEnemy, Melee and the fight goal (the top), each given `durationMs`.
    /// **Coney choice**: the type-3 brain's AttackTarget (9) gate is left out (Coney builds no such goal).
    /// @orig 0x0028d190 Brain_PushFightGoal (unknown)
    void pushFightGoals(int durationMs);
    /// Whether the Melee goal may send the human after its target (`+0x2d3`, set when the brain is made, by a new
    /// target and when a Melee goal ends; cleared when the last move failed).
    /// Its fight books: the active-attacker places and spacing it gives its attackers, and its tackle meter
    /// (docs/research/ai-fights.md#attack-places). A slot claimed on it raises its spacing to the attacker's gang's
    /// `CfgGang` values; its slot list emptied puts the spacing back to 1.
    [[nodiscard]] FightBook& fightBook() { return m_fight; }
    [[nodiscard]] const FightBook& fightBook() const { return m_fight; }
    /// The `CfgGang` fight values of this brain's gang's kind (all 0 without a gang).
    [[nodiscard]] GangFightValues gangFight() const;
    /// Whether the Melee goal may send the human after its target (`+0x2d3`).
    [[nodiscard]] bool mayApproach() const { return m_mayApproach; }
    /// Sets the approach byte.
    void setMayApproach(bool may) { m_mayApproach = may; }
    /// Adds `enemy` to the enemy list (`+0x164`, up to 16), and this brain to `enemy`'s unless both are players'; when
    /// its gang's tactic does not leave the members their own goals, the tactic hears of it (event `0xb`). Nothing when
    /// `enemy` is listed already or the list is full.
    /// @orig 0x0028d538 Brain_AddEnemy (unknown)
    void addEnemy(Brain& enemy);
    /// The enemy list.
    [[nodiscard]] const std::vector<Brain*>& enemies() const { return m_enemies; }
    /// Empties the enemy list (event 2, the reset).
    void clearEnemies() { m_enemies.clear(); }
    /// Takes `target` (null for none) as the target: the old target's attack slot is released and one is claimed on
    /// the new; with `force` a full slot list gives up its farthest holder's slot whatever the distances
    /// (docs/research/ai-fights.md#attack-places).
    /// @orig 0x0028cfe0 Brain_SetTarget (unknown)
    void setTarget(Brain* target, bool force = false);
    /// The cause a target change is counted under (targetChanges()); TargetCause::Other unless a TargetCauseScope
    /// names one.
    [[nodiscard]] TargetCause targetCause() const { return m_targetCause; }
    /// Makes `cause` the one later target changes are counted under; TargetCauseScope sets it and puts it back.
    void setTargetCause(TargetCause cause) { m_targetCause = cause; }
    /// The changes of target so far by cause (a call that keeps the same target is not one), for the trace.
    [[nodiscard]] const std::array<int, kTargetCauseCount>& targetChanges() const { return m_targetChanges; }
    /// The target (`+0x124`); null when none.
    [[nodiscard]] Brain* target() { return m_target; }
    [[nodiscard]] const Brain* target() const { return m_target; }
    /// Whether this brain holds an attack slot on its target.
    /// @orig 0x0028de48 Brain_HasAttackSlot (unknown)
    [[nodiscard]] bool hasAttackSlot() const;
    /// Takes the nearest standing enemy with health as the target when it is not the target already (the fight goal's
    /// re-target, once a second). **Coney choice**: "nearest threat" is the nearest such enemy of the list.
    void retarget();
    /// The attackers holding a slot on this brain's human (`+0x1a4`), and setting how many it allows (`+0x1e4`).
    [[nodiscard]] const std::vector<Brain*>& attackSlots() const { return m_slots; }
    void setAttackSlotCount(std::size_t count);
    /// How many attackers it allows at once (`+0x1e4`).
    [[nodiscard]] std::size_t attackSlotCount() const { return m_slotCount; }
    /// Attackable (`+0x11f`, `GangSetAttackable`; 1 when a brain is made): other AI may attack its human now
    /// (ai::attackableBy()).
    [[nodiscard]] bool attackable() const { return m_attackable; }
    /// Sets the attackable byte.
    void setAttackable(bool attackable) { m_attackable = attackable; }
    /// Threat response (`+0x21c`, `GangSetThreatResponse`): 0 never fights. **Coney choice**: 2 until set, as
    /// `level99` sets its fighters.
    void setThreatResponse(int response) { m_threatResponse = response; }
    /// The threat response (`+0x21c`).
    [[nodiscard]] int threatResponse() const { return m_threatResponse; }
    /// A Warrior hitting back at a player who hit him (`+0x2e5`): he and any pad player are threats to each other
    /// (ai::isThreat()). **Coney stand-in**: the hit-back (`WarriorBrain_OnHitByChief`) is not built, so only a test
    /// sets it.
    [[nodiscard]] bool hittingBack() const { return m_hittingBack; }
    /// Sets the hit-back byte.
    void setHittingBack(bool on) { m_hittingBack = on; }
    /// The turf limit (`+0x14d`): set, a gang with a turf may take enemies only inside it; the gang's alert state
    /// (Gang::setAlertState()) clears it while fighting and sets it again when calm. **Coney stand-ins**: set when a
    /// brain is made (the original's starting value is not on the page), and nothing reads it yet, as Coney keeps no
    /// turf-only target test (docs/research/ai-fights.md#enemy-score).
    [[nodiscard]] bool keepsToTurf() const { return m_keepsToTurf; }
    /// Sets the turf limit.
    void setKeepsToTurf(bool on) { m_keepsToTurf = on; }
    /// The melee ranges (`+0x13c`, `+0x140`, `BrSetMeleeRange`).
    void setMeleeRange(float nearRange, float farRange);
    /// The far and near melee ranges.
    [[nodiscard]] float meleeFar() const { return m_meleeFar; }
    /// The near melee range (`+0x13c`).
    [[nodiscard]] float meleeNear() const { return m_meleeNear; }

    // --- Pacing, chances and rolls.

    /// The earliest time this brain may attack again (`+0x1e8`) and its human may be attacked again (`+0x1ec`), ms.
    [[nodiscard]] std::uint64_t nextAttackMs() const { return m_nextAttackMs; }
    /// Sets the earliest time it may attack again.
    void setNextAttackMs(std::uint64_t ms) { m_nextAttackMs = ms; }
    /// The earliest time its human may be attacked again (`+0x1ec`).
    [[nodiscard]] std::uint64_t attackableAtMs() const { return m_attackableAtMs; }
    /// Sets the earliest time its human may be attacked again.
    void setAttackableAtMs(std::uint64_t ms) { m_attackableAtMs = ms; }
    /// How many attacks on the human were announced since the last update and seen (`+0x200`): the starts (event
    /// `0x10`) within the range and field of view, with a clear line of sight to the attacker.
    [[nodiscard]] int attackWarnings() const { return m_attackWarnings; }
    /// Counts one attack warning (`+0x200`, the shared handler's event `0x10`).
    void countAttackWarning() { ++m_attackWarnings; }
    /// The range and field of view (half-angle, radians) an announced attack must be within (`+0x130`, `+0x12c`).
    void setSight(float range, float fieldOfView);
    /// The sight range (`+0x130`, `HuSetLOSRange`) and field of view (`+0x12c`, `BrSetFOV`).
    [[nodiscard]] float sightRange() const { return m_sightRange; }
    /// The field of view (`+0x12c`, a half-angle in radians).
    [[nodiscard]] float fieldOfView() const { return m_fieldOfView; }
    /// The senses and reactions the story scripts set.
    [[nodiscard]] BrainSenses& senses() { return m_senses; }
    [[nodiscard]] const BrainSenses& senses() const { return m_senses; }
    /// The attack weights (`+0x298`).
    [[nodiscard]] const AttackWeights& attackWeights() const { return m_settings.attackWeights; }
    /// Sets one kind's weight (`BrSetAttackWeight`).
    void setAttackWeight(int kind, std::uint8_t weight);
    /// What it fights by.
    [[nodiscard]] const FightSettings& settings() const { return m_settings; }
    /// The wait an attack of `kind` puts on the next (`Human_AttackDelay`): `CfgAttackDelay` × the power class's
    /// attack delay factor (`+0x1c`), or its factor against a downed target (`+0x20`) when `targetDown`, ms; halved
    /// when `halved` (the target targets this human, or the brain is type 3: the attack action's rule).
    /// @orig 0x00223800 Human_AttackDelay (unknown)
    [[nodiscard]] int attackDelayMs(int kind, bool targetDown, bool halved) const;
    /// The chance to block, percent (`Human_BlockChance`): the power class's block chance (`+0x08`, or `+0x0c` while
    /// hurt) × the base chance to block.
    /// @orig 0x00223628 Human_BlockChance (unknown)
    [[nodiscard]] float blockChance() const;
    /// The chance to press the block's counter on an update, percent (`Human_CounterChance`: `+0x24` × 100).
    /// @orig 0x002235f8 Human_CounterChance (unknown)
    [[nodiscard]] float counterChance() const;
    /// The brain's generator, for its goals' and actions' rolls.
    [[nodiscard]] combat::CombatRandom& random() { return m_random; }
    /// A roll in [0, 100) (the original's rand100).
    [[nodiscard]] int rand100() { return rollRange(m_random, 0, 99); }

    // --- Writing the human's per-player record, as a pad would.

    /// Writes `command` into the record (`+0x20`). Only the command: the record's buttons are a pad's, and an AI has
    /// none, so R1 held (command 4) never starts a block for it (docs/research/ai-fights.md#block).
    /// @orig 0x00147ef0 PlayerRecord_SetCommand (unknown)
    void press(combat::CommandId command);
    /// Sets the move the human's locomotion follows in place of a stick (brain `+0x110` heading, `+0x114` speed): along
    /// `way` across the ground (world axes; its length is ignored) at `speed` m/s. A move action writes no stick
    /// (docs/research/ai-moving.md#moving). It lasts this update only: the next update() clears it first. The move aims
    /// at moveAim() (`+0x90`), the point a fight stance walks the human to.
    void setMove(anim::Vec3 way, float speed);
    /// The same along `heading` (radians, 0 facing +y). At speed 0 the human turns on the spot to it.
    void setMoveHeading(float heading, float speed);
    /// The turn boost (`+0x0b`, `Brain_SetTurnBoost`): an AI human's gait turn limits × (boost + 1), or ÷ (1 − boost)
    /// below 0 (human::aiMaxTurn()); 0 when made. Goals raise it for a run-in and restore it at their end.
    /// @orig 0x0028cdf8 Brain_SetTurnBoost (unknown)
    void setTurnBoost(int boost);
    /// The turn boost (`+0x0b`).
    [[nodiscard]] int turnBoost() const { return m_turnBoost; }
    /// Asks the human to climb this update toward `direction` (the route's climb leg).
    void requestClimb(anim::Vec3 direction);
    /// Stops the move: the human stands (its speed 0) along its facing.
    /// @orig 0x0028ac18 Brain_StopMove (unknown)
    void stopMove();

    // --- Moving over the level (docs/research/ai-moving.md#moving).

    /// The route planner of the level's path data (null when there is none: a move then goes straight).
    [[nodiscard]] RoutePlanner* planner() const { return m_planner; }
    /// Sets the route planner (null for none).
    void setPlanner(RoutePlanner* planner) { m_planner = planner; }
    /// The level's collision the brain's sight rays are cast through (null for none: every line of sight is clear).
    [[nodiscard]] const raycast::CollisionMesh* collision() const { return m_collision; }
    /// Sets the level's collision (null for none).
    void setCollision(const raycast::CollisionMesh* collision) { m_collision = collision; }
    /// Whether this brain's human can see `other`'s (ai::canSeeHuman()) within `range`.
    [[nodiscard]] bool canSee(const Brain& other, float range) const;
    /// Whether the line of sight to `other`'s human is clear (ai::lineOfSight()).
    [[nodiscard]] bool hasLineOfSight(const Brain& other) const;
    /// Why the last move failed (`+0x284`).
    [[nodiscard]] MoveFailure moveFailure() const { return m_moveFailure; }
    /// Sets why the last move failed.
    void setMoveFailure(MoveFailure failure) { m_moveFailure = failure; }
    /// The point a move aims at now and its radius (`+0x90`, `+0x118`): what another human's steering would read.
    [[nodiscard]] anim::Vec3 moveAim() const { return m_moveAim; }
    /// The move aim's radius (`+0x118`).
    [[nodiscard]] float moveAimRadius() const { return m_moveAimRadius; }
    void setMoveAim(anim::Vec3 point, float radius);
    /// The scene's brains (every brain, this one included), which the sector record counts round this one; Brains
    /// gives it (null for none: the record stays empty).
    void setPeers(const std::vector<std::unique_ptr<Brain>>* peers) { m_peers = peers; }
    /// The scene's brains as given (null for none).
    [[nodiscard]] const std::vector<std::unique_ptr<Brain>>* peers() const { return m_peers; }
    /// The steering round humans' state (brain `+0xa0`, docs/research/ai-moving.md#steering).
    [[nodiscard]] SteeringState& steering() { return m_steering; }
    [[nodiscard]] const SteeringState& steering() const { return m_steering; }
    /// The route node a move heads for now (`RouteState_CurrentNode` on the route state at `+0xe0`): the move action
    /// writes it each update, nothing when it goes straight or no move runs.
    [[nodiscard]] std::optional<std::uint32_t> routeNode() const { return m_routeNode; }
    /// Sets the route node the move heads for (nothing for none).
    void setRouteNode(std::optional<std::uint32_t> node) { m_routeNode = node; }
    /// The neighbour sectors round this brain's human, rebuilt first when at least `maxAgeMs` old
    /// (docs/research/ai-moving.md#neighbour-sectors): callers pass kSectorAgeMs, or kSectorGiveWayAgeMs for giving
    /// way.
    /// @orig 0x0028fe90 Brain_GetSectors (unknown)
    [[nodiscard]] Sectors& sectors(std::uint64_t maxAgeMs);
    /// The record as it stands, not refreshed.
    [[nodiscard]] const Sectors& sectorRecord() const { return m_sectors; }
    /// The brain's slot among the scene's brains (its index, which staggers periodic work across brains).
    [[nodiscard]] std::size_t slot() const { return m_slot; }
    /// Sets its slot among the scene's brains.
    void setSlot(std::size_t slot) { m_slot = slot; }
    /// Writes the stick as an attack action does (the snap's): `way` across the ground (world axes; its length is
    /// ignored) at `magnitude`, with the world's +y as the stick's up.
    void writeStick(anim::Vec3 way, float magnitude);
    /// Lets the stick go.
    void releaseStick();

    /// The horizontal distance from this brain's human to `other`'s.
    [[nodiscard]] float distanceTo(const Brain& other) const;
    /// The player's brain nearest this one among the scene's brains (null for none).
    [[nodiscard]] Brain* nearestPlayer() const;
    /// Whether `other`'s human can be fought: it has health left.
    [[nodiscard]] static bool fightable(const Brain& other);
    /// Forgets `other` wherever this brain refers to it (its target, enemies, slots): `other` is going away.
    void forget(const Brain& other);

  private:
    // The peer standing at an announced attack's position (the warning's attacker); null when none.
    [[nodiscard]] Brain* announcer(anim::Vec3 position) const;
    // The goal stack's turn, Brain_UpdateGoals' loop: the top is run (runGoal()); done pops it and the new top runs in
    // the same update, again runs the top again, stop ends the turn.
    // @orig 0x0028fbb0 Brain_UpdateGoals (unknown)
    void processGoals();
    // Starts `goal` when the action queue is empty: its time limit made a deadline from now, its start(), then it is
    // marked started. Returns whether it started.
    // @orig 0x0029ed58 Goal_Start (unknown)
    bool startGoal(Goal& goal);
    // Takes `goal` up again: one never started is started (startGoal()); else it is marked to resume, and with the
    // action queue empty its resume() runs and the mark is cleared. Returns whether it is under way now.
    // @orig 0x0029ee30 Goal_Resume (unknown)
    bool resumeGoal(Goal& goal);
    // One update of `goal`: stop while it cannot start or resume yet (the action queue is busy), done once its deadline
    // has passed (its process() not called), else what its process() returns.
    // @orig 0x0029eed8 Goal_Process (unknown)
    GoalStatus runGoal(Goal& goal);
    // Ends the goal in `slot` when it had started (its end()), destroys it (its destroy()), then moves it to the goals
    // freed after the update.
    // @orig 0x0029edd8 Goal_End (unknown)
    void retireGoal(std::unique_ptr<Goal>& slot);
    // The reaction goal's turn, before the stack's: an active one is run (runGoal()) and, done, ended; then, unless the
    // brain's type, its goals or its human's class hold reactions off, a new one is made from the human's state, the
    // stack's top suspended and the actions cleared. Returns whether the stack is held off this update: a reaction
    // goal still running or just made (it runs from the next update).
    // @orig 0x0028f2b0 Brain_UpdateReactionGoal (unknown)
    bool updateReactionGoal();
    // Ends and frees the reaction goal, then takes the stack's top up again (resumeGoal()); nothing without one.
    // @orig 0x0028f240 Brain_EndReactionGoal (unknown)
    void endReactionGoal();
    // One update of `action` at the front: its delay counted down against the last update's time (a random one drawn
    // first), then, not started, its start() (done: done at once, still not started), then its update().
    // @orig 0x002f9e48 Action_Update (unknown)
    ActionStatus stepAction(Action& action);
    // Whether the finished or cleared `action` may be freed: one that has started is asked to stop (its abort()), and
    // refuses with false; one never started is not asked (docs/research/ai/index.md#actions).
    // @orig 0x002f9de0 Action_AbortAndDestroy (unknown)
    bool finishAction(Action& action);
    // The actions' turn: the front action is stepped (stepAction()); done, it is freed and the next is stepped in the
    // same update, until one is still running, the queue is empty or one refuses to stop.
    // @orig 0x0028fe28 Brain_RunActions (unknown)
    void runActions();
    // Claims a slot for `attacker` on this brain's human: a free one, else the farthest holder's when `attacker` is
    // closer or forced in, and that holder loses his target; returns whether it holds one.
    // @orig 0x0028df30 Brain_ClaimAttackSlot (unknown)
    bool claimSlot(Brain& attacker, bool force = false);
    // Releases `attacker`'s slot on this brain's human.
    void releaseSlot(const Brain& attacker);
    // Raises this human's spacing bytes to `attacker`'s gang's `CfgGang` values (a Warrior keeps one swinging at
    // him on his feet).
    void raiseSpacing(const Brain& attacker);
    // Adds `enemy` to the list only; returns whether it was added.
    bool listEnemy(Brain& enemy);

    human::Human* m_human;
    std::uint64_t m_nowMs = 0;
    std::uint64_t m_lastUpdateMs = 0;                                  // +0x30
    std::uint64_t m_updates = 0;                                       // +0x34
    std::uint64_t m_thinks = 0;                                        // +0x38
    std::unique_ptr<Goal> m_reaction;                                  // +0x3c
    std::vector<std::unique_ptr<Goal>> m_goals;                        // +0x40, the top at the back
    std::array<std::unique_ptr<Action>, kActionQueueSize> m_actions{}; // +0x68, circular
    std::size_t m_actionCount = 0;                                     // +0x2e
    std::size_t m_actionFront = 0;                                     // +0x2f
    Brain* m_target = nullptr;                                         // +0x124
    std::vector<Brain*> m_enemies;                                     // +0x164
    Sectors m_sectors;                                                 // 0x006e8318 + slot × 0x48
    const std::vector<std::unique_ptr<Brain>>* m_peers = nullptr;
    std::vector<Brain*> m_slots;                   // +0x1a4
    std::size_t m_slotCount = kDefaultAttackSlots; // +0x1e4
    std::uint64_t m_nextAttackMs = 0;              // +0x1e8
    std::uint64_t m_attackableAtMs = 0;            // +0x1ec
    RoutePlanner* m_planner = nullptr;
    const raycast::CollisionMesh* m_collision = nullptr;
    SteeringState m_steering; // +0xa0
    std::size_t m_slot = 0;
    double m_handle = 0;              // the human's script handle
    Gang* m_gang = nullptr;           // +0x20c
    Formation* m_following = nullptr; // +0x212
    PadControl m_padControl;
    ScriptServices* m_services = nullptr;
    FightBook m_fight; // +0x1f0, +0x14a, +0x14b, +0x148
    // Goals popped while one of them may still be running (a goal's Process can start a new fight, which pops it):
    // freed once the update is over.
    std::vector<std::unique_ptr<Goal>> m_retired;
    // The four-byte fields after the eight-byte ones, and the one-byte ones last, so the fields pack without padding.
    FightSettings m_settings;
    combat::CombatRandom m_random;
    int m_goalBase = -1;                       // +0x2d
    int m_attackWarnings = 0;                  // +0x200
    float m_sightRange = kDefaultSightRange;   // +0x130
    float m_fieldOfView = kDefaultFieldOfView; // +0x12c
    BrainSenses m_senses;
    int m_threatResponse = kDefaultThreatResponse; // +0x21c
    float m_meleeNear = kDefaultMeleeNear;         // +0x13c
    float m_meleeFar = kDefaultMeleeFar;           // +0x140
    int m_goalsRanOut = 0;
    int m_helpCalls = 0;                                  // Coney's count for the trace
    int m_helpersSent = 0;                                // Coney's count for the trace
    std::array<int, kTargetCauseCount> m_targetChanges{}; // Coney's count for the trace
    TargetCause m_targetCause = TargetCause::Other;
    anim::Vec3 m_moveAim;                     // +0x90
    float m_moveAimRadius = 0.0F;             // +0x118
    std::optional<std::uint32_t> m_routeNode; // RouteState_CurrentNode of +0xe0
    int m_characterClass = -1;                // the human's class
    int m_seenHealth = -1;
    int m_unreportedDamage = 0;
    int m_turnBoost = 0; // +0x0b
    BrainType m_type;
    bool m_enabled = true;
    MoveFailure m_moveFailure = MoveFailure::None; // +0x284
    bool m_dead = false;                           // +0x09
    bool m_givingWay = false;                      // +0xcc bit 1
    bool m_pushingAside = false;                   // +0xcc bit 4
    bool m_knockedOut = false;                     // +0x08 cleared
    bool m_suspended = false;                      // +0x0a
    bool m_wantsWeapon = true;                     // +0x265
    bool m_downReported = false;
    bool m_lyingReported = false;
    bool m_mayApproach = true;  // +0x2d3
    bool m_attackable = true;   // +0x11f
    bool m_hittingBack = false; // +0x2e5
    bool m_keepsToTurf = true;  // +0x14d
};

/// Names the cause of the target changes `brain` makes while it lives (Brain::targetChanges()), and puts the cause
/// before it back when it ends, so a nested cause (a hit back inside a pick) wins only for its own changes.
class TargetCauseScope {
  public:
    /// Names `cause` on `brain` until the scope ends.
    TargetCauseScope(Brain& brain, TargetCause cause) : m_brain(brain), m_before(brain.targetCause()) {
        brain.setTargetCause(cause);
    }
    /// Puts the cause before it back.
    ~TargetCauseScope() { m_brain.setTargetCause(m_before); }
    TargetCauseScope(const TargetCauseScope&) = delete;
    TargetCauseScope& operator=(const TargetCauseScope&) = delete;
    TargetCauseScope(TargetCauseScope&&) = delete;
    TargetCauseScope& operator=(TargetCauseScope&&) = delete;

  private:
    Brain& m_brain;
    TargetCause m_before;
};

/// Delivers `event` to `brain`'s human as `Human_OnEvent` does: its own script handlers first (`SetMsgHandler`, through
/// the brain's services, `0x00384c38`), then, unless they took it, its gang (`Gang_OnEvent`), then, unless the gang
/// used it, its brain (Brain::onEvent()). Returns whether any used it.
/// @orig 0x0021d480 Human_OnEvent (unknown)
bool deliverEvent(Brain& brain, const BrainEvent& event);

/// The brain side of `Human_ReleasePlayer(human, 0, 1)` (docs/research/characters.md#players): the
/// player's `brain` goes back to `leftAs` (its class's type, as `Human_Init` chose it), its goals and actions are
/// cleared, it loses the pad's switch (Brain::setType()) and its human takes the flags releasedFlags() gives. The pad,
/// the panel and the player list are the caller's (ai::AiHumans::releasePlayer()).
/// @orig 0x0022a2a8 Human_ReleasePlayer (unknown)
void releaseToAi(Brain& brain, BrainType leftAs);

/// `brain`'s reaction kind, which picks its gesture slots (docs/research/ai-fights.md#fight-reposition): its type, but
/// 0 for a gang brain of class role 13, and 3 for the player. **Coney stand-ins**: the player's 0 in co-op (game state
/// `+0x158` set) is not read; the AI has no co-op word, so the player is taken as alone. The class role is read
/// through the gang's lookups (as helpLevel()'s is), so a gang brain with no gang keeps its type, 2.
/// @orig 0x00291d48 Brain_GetReactionKind (unknown)
[[nodiscard]] int reactionKind(const Brain& brain);

/// How many of its reaction kind's two gesture slots `brain` may take: 1 for a cop or a Warrior, and for a gang
/// brain or a civilian of class role 6 or whose gang runs the confront tactic (`0x23`); else 2. **Coney stand-in**:
/// the class role is read through the gang's lookups, so a gang brain or a civilian with no gang takes 2.
/// @orig 0x00291db8 Brain_GetHelpLevel (unknown)
[[nodiscard]] int helpLevel(const Brain& brain);

/// A scene's release of `human` (an AI human; the player's is the level's): placed at `feet` facing `headingDegrees`
/// (Human::place(), dropped onto `ground` when not null), its motion reset as at the scene's start, and, only when
/// `skipped`, its brain's part of the placement (Brain::placed(); `brain` may be null). After a skip the original
/// places the human at its end pose through `Human_SetTransform`, which pops the front move; after a clip that played
/// to its end it places no one, so nothing pops (docs/research/scenes.md#ending, teleports.md#in-coney).
/// @orig 0x0023d440 Human_SetTransform (unknown)
void placeAfterScene(human::Human& human, Brain* brain, const raycast::CollisionMesh* ground, anim::Vec3 feet,
                     float headingDegrees, bool skipped);

} // namespace coney::ai
