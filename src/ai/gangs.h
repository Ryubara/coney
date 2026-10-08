// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ai/brain.h"
#include "ai/tactic.h"
#include "combat/stick.h"

// The gangs: 32 records that group humans, say which gangs are enemies or friends of which, hold a tactic, the
// scripts' message handlers, and a suspended flag that stops the members' brains. A human's events reach its gang
// before its brain (Gang_OnEvent before Brain_OnEvent); the level scripts end their fights on a gang's event 18,
// which passes how many members are still standing.
// Research: docs/research/ai.md#gang-record, docs/research/ai.md#gang-events, docs/research/ai.md#gang-update

namespace coney::ai {

class Gangs;
class ScriptServices;

/// The gang records (`0x005e6e30`, 32 of 0xb10), and the size of a gang's member list (`+0x48`).
inline constexpr std::size_t kGangSlots = 32;
inline constexpr std::size_t kGangMemberSlots = 16;
/// A gang holds at most this many members, kGangMemberSlots for the police kind and kind `0x17` (`0x00166308`).
inline constexpr std::size_t kGangMembers = 10;
/// The gang kinds the research names: the Warriors (0), the police (1) and the kind that counts as police for
/// friendship (`0x17`).
inline constexpr int kWarriorsKind = 0;
inline constexpr int kPoliceKind = 1;
inline constexpr int kPoliceLikeKind = 0x17;
/// The message ids `Gang_OnEvent` passes its own arguments to: a member down (18: the attacker and the members still
/// standing), a member's headcount event (2) and an arrest (`0x11`: the event's value).
inline constexpr int kGangMessageDown = 18;
inline constexpr int kGangMessageHeadcount = 2;
/// A member left (value 0) or joined (value 1) the gang (event `0x16`, from `Gang_RemoveMember` and `Gang_AddMember`).
inline constexpr int kGangMessageMembers = 0x16;
inline constexpr int kGangMessageArrest = 0x11;

/// A gang's turf holds at most this many volume boxes (`+0x15c`, count `+0x17c`).
inline constexpr std::size_t kTurfBoxes = 8;

/// What the story scripts set on a gang beyond its members, masks and tactic (docs/references/bindings/gang.md).
struct GangOrders {
    double leader = 0;                     ///< `+0x44`: the leader's handle (`GangSetLeader`); 0 for none.
    std::array<double, kTurfBoxes> turf{}; ///< `+0x15c`: the turf's volume boxes; 0 for an empty slot.
    int respondPercent = 0;                ///< `+0xd7`: members that may help at once, percent.
    bool attackStrategies = false;         ///< `+0xd9`: the coordinated attacks (not built in Coney).
    /// `GangExitWorld`: the members are leaving; the callback runs once the last has gone, and the gang is then
    /// deleted unless `keepWhenEmpty`.
    bool exiting = false;
    std::string exitCallback;
    bool keepWhenEmpty = false;
};

/// One gang.
class Gang {
  public:
    /// Its id (`+0x30`): its slot, and its bit in other gangs' masks.
    [[nodiscard]] int id() const { return m_id; }
    /// Its kind (`+0x2c`, `GangCreate`'s type) and name.
    [[nodiscard]] int kind() const { return m_kind; }
    [[nodiscard]] const std::string& name() const { return m_name; }
    /// In use (`+0x18`).
    [[nodiscard]] bool inUse() const { return m_inUse; }
    /// The enemy and friend masks (`+0x38`, `+0x3c`): a bit per gang id.
    [[nodiscard]] std::uint32_t enemyMask() const { return m_enemies; }
    [[nodiscard]] std::uint32_t friendMask() const { return m_friends; }
    /// Suspended (`+0xd4`): its members' brains skip their update.
    [[nodiscard]] bool suspended() const { return m_suspended; }
    /// Invincible (`+0xd8`, `GangInvincible`): its members, and those who join later, are gods (human flag `0x10`).
    [[nodiscard]] bool invincible() const { return m_invincible; }
    /// Always seen (`+0xdc`, `GangSetAlwaysSeen`): an enemy's scan notices its members anywhere in sight range, and its
    /// own members' fight and spectate goals skip the line-of-sight test. **Coney stand-in**: Coney's brains have no
    /// enemy scan yet, so it is only kept.
    [[nodiscard]] bool alwaysSeen() const { return m_alwaysSeen; }
    /// The members (`+0x48`), in the order they joined.
    [[nodiscard]] const std::vector<Brain*>& members() const { return m_members; }
    /// The tactic (`+0x40`); null for none.
    [[nodiscard]] Tactic* tactic() const { return m_tactic.get(); }
    /// How many times a tactic was set on it (Gangs::setTactic()): a change says a newer order came.
    [[nodiscard]] std::uint32_t tacticsSet() const { return m_tacticsSet; }
    /// The Lua handler of message `message` (`+0xe4 + message × 4`); empty for none.
    [[nodiscard]] std::string_view handler(int message) const;
    /// The members still on their feet (`0x00166220`): health left and not on the ground. **Coney choice** for the
    /// three state tests (`0x00227dd8`, `0x00227eb0`, `0x00227d98`: down, dead and a third state, not traced).
    /// @orig 0x00166220 Gang_StandingCount (unknown)
    [[nodiscard]] int standing() const;
    /// The gangs it belongs to.
    [[nodiscard]] Gangs& owner() const { return *m_owner; }
    /// What the story scripts set on it.
    [[nodiscard]] GangOrders& orders() { return m_orders; }
    [[nodiscard]] const GangOrders& orders() const { return m_orders; }
    /// The turf boxes in use (`+0x17c`).
    [[nodiscard]] std::size_t turfCount() const;
    /// The gang's leader (`0x00165678`): the leader set while it is a member, alive, not a player and not down; else
    /// the first member that is. Null when none is. **Coney choice**: "down" is Coney's grounded state.
    /// @orig 0x00165678 Gang_GetLeader (unknown)
    [[nodiscard]] Brain* leader() const;
    /// The gang's chosen target (`+0x10`): the enemy its members score 400 points more and the Warriors may hit when a
    /// street civilian (ai::attackableBy()); null for none.
    [[nodiscard]] Brain* chosenTarget() const { return m_chosenTarget; }
    void setChosenTarget(Brain* target) { m_chosenTarget = target; }

    /// `member`'s event, before its brain has it (`Gang_OnEvent`): while scripts run, the message handler for the
    /// event's id is called: for 18 with (member, other, standing()), for 2 with (member, other, the headcount), for
    /// `0x11` with (member, other, the event's value), none of which uses the event; for any other id with (member,
    /// other, the event's value), which uses it when the function returns true (**Coney choice** of the arguments: the
    /// generic marshaller `0x00384ce0` is not traced). Then the tactic has it, when the gang has members (with none,
    /// only a member leaving: kGangMessageMembers with 0); a tactic not yet started is first processed once, as the
    /// gang's update would, so its start's goals are on the members before the event gives any again. Returns whether
    /// it was used.
    /// @orig 0x00164c20 Gang_OnEvent (unknown)
    bool onEvent(Brain& member, const BrainEvent& event);

  private:
    friend class Gangs;

    // The tactic's update (`Tactic_Process`, from the gang's update `0x00166708`), its callback fired on a non-zero
    // result. The tactic must be set.
    void processTactic();

    Gangs* m_owner = nullptr;
    int m_id = -1;
    int m_kind = 0;
    std::string m_name;
    bool m_inUse = false;
    std::uint32_t m_enemies = 0;
    std::uint32_t m_friends = 0;
    bool m_suspended = false;
    bool m_invincible = false;
    bool m_alwaysSeen = false;
    std::vector<Brain*> m_members;
    Brain* m_chosenTarget = nullptr;
    std::unique_ptr<Tactic> m_tactic;
    std::uint32_t m_tacticsSet = 0; // tactics set so far
    std::map<int, std::string> m_handlers;
    GangOrders m_orders;
};

/// The gangs of a level, by id.
class Gangs {
  public:
    /// No gangs in use; a tactic's rolls are drawn from a generator seeded with `seed`.
    explicit Gangs(std::uint32_t seed = 1);
    Gangs(const Gangs&) = delete;
    Gangs& operator=(const Gangs&) = delete;
    Gangs(Gangs&&) = delete;
    Gangs& operator=(Gangs&&) = delete;
    ~Gangs() = default;

    /// The script services the message handlers and tactics call through (null for none: no handler is called).
    void setScripts(ScriptServices* scripts) { m_scripts = scripts; }
    [[nodiscard]] ScriptServices* scripts() const { return m_scripts; }

    /// `GangCreate(kind, name)`: takes the first free slot for a gang of `kind` named `name`. Returns its id, or -1
    /// when the name is in use or every slot is taken.
    /// @orig 0x0016cdf0 Gang_Create (unknown)
    [[nodiscard]] int create(int kind, std::string_view name);
    /// `GangDelete`: the members leave, the tactic ends and the slot is free again. Nothing for an id not in use.
    void remove(int id);
    /// The gang with `id`, when it is in use; null otherwise (and for -1).
    [[nodiscard]] Gang* find(int id);
    [[nodiscard]] const Gang* find(int id) const;

    /// `GangAddMember`: `brain` leaves its gang, even when it is gang `id` (its actions and target cleared, under a
    /// tactic popped to its goal base, no longer the leader, event kGangMessageMembers with 0), then joins gang `id`
    /// (a player becomes its leader, the war chief; event kGangMessageMembers with 1), so a follow tactic gives the
    /// members their goals again (docs/research/ai.md#warrior-follow). A full gang drops its first member first
    /// (**Coney choice** of which). Nothing for an id not in use. **Coney choices**: the brain's counted enemies are
    /// kept; any player joining leads, where the original makes only a war chief (`+0x3ac`) the leader.
    /// @orig 0x00166308 Gang_AddMember (unknown)
    void addMember(int id, Brain& brain);
    /// `brain` leaves its gang, if it has one.
    void removeMember(Brain& brain);

    /// Gangs `a` and `b` become enemies both ways: the friend bit cleared, the enemy bit set. Nothing when either is
    /// not in use (and for -1).
    /// @orig 0x0016acf0 GangMakeEnemies (unknown)
    void makeEnemies(int a, int b);
    /// Gangs `a` and `b` become friends both ways: the enemy bit cleared, the friend bit set.
    /// @orig 0x0016ad80 GangMakeFriends (unknown)
    void makeFriends(int a, int b);
    /// Whether `a` and `b` are friends (`0x00168f58`): the same gang or kind, both kinds police-like (1 or `0x17`), or
    /// the friend bit. **Coney choices**: a human with no gang is no one's friend, and the global truce
    /// (`0x0050cb7c`) is not built.
    /// @orig 0x00168f58 Gang_AreFriends (unknown)
    [[nodiscard]] static bool friends(const Gang* a, const Gang* b);
    /// `GangMakeNeutralOfType(id, kind)`: gang `id` and every other gang in use of kind `kind` lose each other's
    /// enemy and friend bits, both ways, unless the two are friends already (friends()). Nothing for a gang not in use.
    /// **Coney's**: the members' brain byte `+0x290` the original clears for kind 0 is not modelled (its meaning is not
    /// traced).
    /// @orig 0x0016ae90 Gang_MakeNeutralOfTypeById (unknown)
    /// @orig 0x0016c470 Gang_MakeNeutralWithType (unknown)
    void makeNeutralOfType(int id, int kind);
    /// `GangMakeEnemiesOfType(id, kind)`: gang `id` and every gang in use of kind `kind` become enemies both ways (the
    /// friend bit cleared, the enemy bit set), the gang itself too when it is of that kind. Gangs made later are not
    /// affected. Nothing for a gang not in use.
    /// @orig 0x0016ae60 Gang_MakeEnemiesOfType (unknown)
    /// @orig 0x0016c3a8 Gang_SetHostileToKind (unknown)
    void makeEnemiesOfType(int id, int kind);
    /// `GangSetAlwaysSeen`: the always-seen byte. Nothing for a gang not in use.
    /// @orig 0x0016bde8 Gang_SetAlwaysSeen (unknown)
    void setAlwaysSeen(int id, bool on);
    /// Whether gang `a` has `b` as an enemy (the enemy bit).
    [[nodiscard]] static bool enemies(const Gang* a, const Gang* b);

    /// `GangSetThreatResponse`: every current member's brain takes `response` (later members keep their own).
    /// @orig 0x0016b3d0 Gang_SetThreatResponse (unknown)
    void setThreatResponse(int id, int response);
    /// `GangBrDead`: every member's brain set dead or alive (Brain::setDead()).
    /// @orig 0x0016aa98 Gang_SetBrainsDead (unknown)
    void setDead(int id, bool dead);
    /// `GangBrFlush`: every member's brain flushed (Brain::flush()).
    void flush(int id);
    /// `GangSuspend`: the suspended flag.
    /// @orig 0x0016a220 Gang_Suspend (unknown)
    void suspend(int id, bool suspended);
    /// `GangInvincible`: every current member made invincible or mortal, and the setting kept for those who join
    /// later. **Coney's reading**: invincible is god mode (human flag `0x10`); what the original sets on each member
    /// (`0x0016a000`) is not described.
    /// @orig 0x0016a260 Gang_SetInvincible (unknown)
    void setInvincible(int id, bool on);
    /// `GangSetTargetable`: whether the current members can be targeted (brain `+0x120`, kept on the human:
    /// human::ScriptState::targetable). Later members keep their own.
    /// @orig 0x0016bac0 Gang_SetTargetable (unknown)
    void setTargetable(int id, bool on);
    /// `GangSetAttackable`: the attackable byte (brain `+0x11f`) of every current member; later members keep their own.
    /// @orig 0x0016bb58 Gang_SetAttackable (unknown)
    void setAttackable(int id, bool on);
    /// Forgets `brain` as any gang's chosen target: it is going away.
    void forget(const Brain& brain);
    /// `GangSetMsgHandler`: the Lua function `function` handles message `message` (empty clears it).
    /// @orig 0x00164bb8 Gang_SetMessageHandler (unknown)
    void setMessageHandler(int id, int message, std::string function);
    /// Gives gang `id` `tactic` (null clears it): the old one ends, and is freed at the end of the next
    /// update(). Nothing for an id not in use.
    /// @orig 0x00165640 Gang_SetTactic (unknown)
    void setTactic(int id, std::unique_ptr<Tactic> tactic);

    /// One step of the gangs at game time `nowMs` (`Gangs_Update`'s tactic part): each gang in use with members runs
    /// its tactic (Tactic::process()), whose non-zero result fires its callback with that result. **Coney choices**:
    /// the bounds, the return to calm, the neutral rule, the spawners and the alert state's anim substitutions are not
    /// built; what the callback is given besides the gang is not traced.
    /// @orig 0x0016d170 Gangs_Update (unknown)
    void update(std::uint64_t nowMs);

    /// The game time of the last update, ms; the tactics' clock.
    [[nodiscard]] std::uint64_t nowMs() const { return m_nowMs; }
    /// The generator the tactics roll with.
    [[nodiscard]] combat::CombatRandom& random() { return m_random; }

  private:
    std::array<Gang, kGangSlots> m_gangs{};
    std::vector<std::unique_ptr<Tactic>> m_retired; // tactics replaced, freed by the next update
    ScriptServices* m_scripts = nullptr;
    std::uint64_t m_nowMs = 0;
    combat::CombatRandom m_random;
};

} // namespace coney::ai
