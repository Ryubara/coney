// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "combat/combat_tuning.h"
#include "combat/meters.h"

// The rage a hit gives its attacker. The stats system turns each attack into two awards, event 2 (1 point) × count A
// and event 1 (4 points) × count B, each passed through the rage formula and truncated on its own; a blocked hit halves
// each award's points with a shift. Being hit gives nothing.
// Research: docs/research/combat.md#rage, docs/research/combat.md#being-hit-runtime

namespace coney::combat {

/// The points of event 2 (table 3 entry 2) and event 1 (table 3 entry 1), as `CfgSetStatValue` fills them.
inline constexpr int kRageEvent2Points = 1;
inline constexpr int kRageEvent1Points = 4;

/// The points of a throw's two awards (147-161). **Coney's choice**, back-derived from the runtime rage: a throw gave
/// 8 and 7 rage at gain 144 and 13 and 11 with a throw bonus of 1.54 (docs/research/combat.md#rage), which
/// `trunc(points × 1.44 × bonus)` gives for 6 and 5 points; the events the throws map to (`0x00264fa0`) are not traced.
inline constexpr std::array<int, 2> kThrowAwardPoints{6, 5};

/// The two award counts of one attack.
struct RageCounts {
    int event2 = 0; ///< Count A: awards of kRageEvent2Points.
    int event1 = 0; ///< Count B: awards of kRageEvent1Points.
};

/// The counts attack `animId` awards: the chain attacks 11-20 and the moving attacks 21-24 from the research's table;
/// **Coney's choice** for the two grab moves seen at runtime but not mapped to events: the counter 76 (7 points, 10
/// rage at runtime) and the strike back 104 (1 point, 1 rage); **Coney stand-in**: an anim set's square and cross
/// (combat::animSetClips(), a bat's 34 and 36) count as `S1` and `X1`; every other id awards nothing.
[[nodiscard]] RageCounts rageCounts(int animId);

/// The points of one award: `count × value`, halved with a shift for a blocked hit (`0x004ed9c8`).
[[nodiscard]] int awardPoints(int count, int value, bool blocked);

/// What a noted hit counts as for the repeat tracker.
enum class RepeatKind : std::uint8_t {
    Square = 0, ///< Square-ended: 12, 14, 16, 19, 20, 35, 40, 46.
    Cross = 1,  ///< Cross-ended: 11, 13, 15, 17, 18.
    Mount = 2,  ///< The mount strikes 219, 221, 223.
    Grab = 3,   ///< The grab strikes 51, 53, 55.
    Other = 4,  ///< Everything else.
};

/// The kind of a hit by attack `animId`.
[[nodiscard]] RepeatKind repeatKind(int animId);

/// A player's repeat tracker (`*(0x0051489c) + 0x178 + player × 0x5c`): the same kind of hit six times in a row, each
/// within 5000 ms of the last, halves the rage of every later hit of the run; each grab or mount strike raises the
/// throw bonus by 0.27, up to 2.0.
class RepeatTracker {
  public:
    /// Notes a hit (landed or blocked) by attack `animId` at `nowMs`, after its rage was given: the flag is cleared;
    /// both counts restart for an other kind, a change of kind or a gap over 5000 ms; a square or cross kind counts one
    /// (a count reaching 6 is held at 5 and sets the flag); a grab or mount strike adds 0.27 to the bonus.
    /// @orig 0x00265dd0 Rage_NoteHit (unknown)
    /// @orig 0x00418428 RepeatTracker_Note (unknown)
    void note(int animId, std::uint64_t nowMs);
    /// Puts the throw bonus back to 1.0 (every update the player is neither grabbing from the front nor throwing).
    void resetBonus() { m_bonus = 1.0F; }
    /// The repeat flag: the next hits' rage is halved.
    [[nodiscard]] bool halved() const { return m_halved; }
    /// The throw bonus.
    [[nodiscard]] float bonus() const { return m_bonus; }
    /// The run's count of `kind` (square or cross), 0 to 5; 0 for the other kinds.
    [[nodiscard]] int count(RepeatKind kind) const;

  private:
    std::uint64_t m_lastMs = 0;
    float m_bonus = 1.0F;
    int m_squareCount = 0;
    int m_crossCount = 0;
    bool m_halved = false;
    RepeatKind m_lastKind = RepeatKind::Other;
};

/// Gives the attacker of a hit of `animId` its rage (blocked or not) at game time `nowMs`: the two awards in turn (a
/// throw's kThrowAwardPoints), each through RageMeter::add() (which holds the meter 5 s and gives nothing in rage),
/// halved while `tracker`'s flag is set and times its throw bonus when `throwing`. Returns what was added. The caller
/// notes the hit in `tracker` afterwards.
/// @orig 0x002653d8 Stats_AttackRage (unknown)
int awardHitRage(RageMeter& rage, int animId, bool blocked, const CombatTuning& tuning, std::uint64_t nowMs,
                 const RepeatTracker& tracker, bool throwing = false);

} // namespace coney::combat
