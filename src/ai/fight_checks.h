// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>

// The fight goal's two waits: whether attacker A may attack target T now (`Brain_CheckAttack`, a "go" or a reason to
// wait), and, when he must wait, the ring round T he holds meanwhile (`FightGoal_Reposition`'s band and limit) and
// whether he taunts. Pure decisions; the fight goal fills them from the two brains.
// Research: docs/research/ai.md#check-attack, docs/research/ai.md#fight-reposition

namespace coney::ai {

/// `Brain_CheckAttack`'s answers: 1 goes, every other is a reason to wait.
namespace check {
inline constexpr int kNoKind = 0;          ///< No kind chosen, or no place free.
inline constexpr int kGo = 1;              ///< Attack now.
inline constexpr int kCoolingDown = 2;     ///< A's own next-attack time has not come.
inline constexpr int kNotAttackable = 3;   ///< T may not be attacked by A.
inline constexpr int kArmedNearLeader = 4; ///< Hold back from an armed man near the leader.
inline constexpr int kPolice = 5;          ///< The police have him.
inline constexpr int kFriendInLine = 6;    ///< Do not throw through a friend.
inline constexpr int kBossHeld = 7;        ///< Do not hit the boss while a player has him.
inline constexpr int kMateFirst = 8;       ///< Let a mate with a heavy object go first.
} // namespace check

/// What `Brain_CheckAttack` tests, in its order; each flag is the whole of one row's test, worked out by the caller.
struct CheckAttackInput {
    bool cooledDown = true;         ///< B's next-attack time (`+0x1e8`) has come.
    bool kindChosen = true;         ///< The kind is not 45.
    bool attackable = true;         ///< `Brain_IsAttackableBy(T, A)`.
    bool chargeOutOfStance = false; ///< A charge kind (0, 19-21) while A is out of the fight stance.
    bool armedNearLeader = false;   ///< Row 4.
    bool mateGoesFirst = false;     ///< Row 8.
    bool policeHaveHim = false;     ///< Row 5.
    bool friendInLine = false;      ///< Row 6.
    bool bossHeldByPlayer = false;  ///< Row 7.
    bool grabKind = false;          ///< The kind is 22.
    bool behindTarget = false;      ///< A is the near human in sector 3, 4 or 5 of T's record (kind 22).
};

/// Whether A may attack now: the first row that applies, in the page's order; `claimPlace` (A takes or has one of T's
/// active-attacker places) is called only when every earlier row passed.
/// @orig 0x002906b8 Brain_CheckAttack (unknown)
[[nodiscard]] int checkAttack(const CheckAttackInput& input, const std::function<bool()>& claimPlace);

/// The band a waiting attacker holds round his target, metres, and the move's limit.
struct RepositionRing {
    float inner = 0.0F;
    float outer = 0.0F;
    std::uint32_t limitMs = 0;
};

/// What the reposition reads.
struct RepositionInput {
    int reason = check::kNoKind;    ///< Brain_CheckAttack's answer.
    bool targetEmptyHanded = true;  ///< T holds nothing.
    bool bothArmed = false;         ///< T and A both hold something...
    bool fewSlotHolders = true;     ///< ... and A is one of fewer than two slot holders on T.
    bool targetFacesMe = false;     ///< T's own target is A...
    bool firstOnFreeTarget = false; ///< ... or T has none and A is first in T's slot list.
    bool tacticRing = false;        ///< A's gang has a tactic of type 2 (Defend) or `0xd`.
    bool cop = false;               ///< A is a cop (type 1).
    float targetRadius = 0.0F;      ///< T's capsule radius.
    float nearRange = 3.0F;         ///< A's melee ranges.
    float farRange = 5.0F;
};

/// Whether A is close in (the one T is fighting stays close).
[[nodiscard]] bool closeIn(const RepositionInput& input);

/// The ring: inner 2 × T's radius when close in or in a tactic ring, else (near + far) / 2; outer 0.95 × far, at least
/// inner + 0.1 m; for reason 5 a non-cop keeps 3 × far to 3 × far + 1 m; the limit 2000 ms (6000 for reason 5).
/// @orig 0x002b2fc8 FightGoal_Reposition (unknown)
[[nodiscard]] RepositionRing repositionRing(const RepositionInput& input);

/// Whether the waiting attacker taunts this time (its 1-2 s timer having run): T at least 2.75 m off, fewer than two
/// humans holding slots on A, A not close in and not in a tactic ring, T empty-handed, gestures allowed; for reason 7
/// only when `roll100` < 11.
[[nodiscard]] bool repositionTaunts(const RepositionInput& input, float distance, int slotsOnMe, bool gesturesAllowed,
                                    int roll100);

/// The tackle meter (brain `+0x148`): it fills while the target keeps running off or swings a weapon, and the fight
/// goal's tackle try fires once it passes the gang's threshold.
/// Research: docs/research/ai.md#try-tackle
class TackleMeter {
  public:
    /// The meter's cap.
    static constexpr int kCap = 36;

    /// One think (the cop's, the gang soldier's and the Warrior's, every 0.2 s): it rises by the target's gait
    /// (`+0x1a8`) while `pressing` (the target moves and holds a weapon or moves away from the thinker), capped at
    /// kCap; otherwise it falls by 2, not below 0.
    void think(bool pressing, int targetGait);
    /// Whether the try may fire for a gang whose `CfgGang` value 7 is `gangTackle`: the value above 0 and the meter
    /// above (7 − value) × 6 (so a value of 1 never fires).
    [[nodiscard]] bool ready(int gangTackle) const;
    /// Emptied (a tackle queued, or no tackle in the gang or the table).
    void clear() { m_value = 0; }
    [[nodiscard]] int value() const { return m_value; }

  private:
    int m_value = 0;
};

} // namespace coney::ai
