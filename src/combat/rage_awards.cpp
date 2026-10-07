// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/rage_awards.h"

#include <algorithm>
#include <array>
#include <utility>

#include "combat/anim_ids.h"

namespace coney::combat {

namespace {

// One attack's counts of events 2 and 1.
struct CountRow {
    int animId;
    int event2;
    int event1;
};

// The research's chain and moving attacks (docs/research/combat.md#rage), then Coney's two grab moves.
constexpr std::array<CountRow, 16> kCounts{{
    {.animId = 11, .event2 = 0, .event1 = 1},
    {.animId = 12, .event2 = 1, .event1 = 0},
    {.animId = 13, .event2 = 1, .event1 = 1},
    {.animId = 14, .event2 = 4, .event1 = 1},
    {.animId = 15, .event2 = 2, .event1 = 2},
    {.animId = 16, .event2 = 1, .event1 = 0},
    {.animId = 17, .event2 = 3, .event1 = 2},
    {.animId = 18, .event2 = 3, .event1 = 2},
    {.animId = 19, .event2 = 3, .event1 = 0},
    {.animId = 20, .event2 = 3, .event1 = 0},
    {.animId = 21, .event2 = 1, .event1 = 0},
    {.animId = 22, .event2 = 1, .event1 = 0},
    {.animId = 23, .event2 = 1, .event1 = 0},
    {.animId = 24, .event2 = 1, .event1 = 0},
    {.animId = 76, .event2 = 7, .event1 = 0},
    {.animId = 104, .event2 = 1, .event1 = 0},
}};

// Counts of one event: `count` awards of event 2, 1, 0, 3 or 5.
RageCounts onlyEvent(int event, int count) {
    RageCounts counts;
    switch (event) {
    case 0:
        counts.event0 = count;
        break;
    case 1:
        counts.event1 = count;
        break;
    case 2:
        counts.event2 = count;
        break;
    case 3:
        counts.event3 = count;
        break;
    default:
        counts.event5 = count;
        break;
    }
    return counts;
}

// "The other attacks" (confirmed (code) at 0x002653d8): one award each, by id range.
RageCounts otherAttack(int animId) {
    const auto in = [animId](int first, int last) { return animId >= first && animId <= last; };
    if (in(0, 1)) {
        return onlyEvent(0, 1);
    }
    if (in(0x19, 0x1e)) {
        return onlyEvent(5, 1);
    }
    if (animId == 34 || animId == 39 || in(45, 46)) {
        return onlyEvent(2, 2); // a set's square
    }
    if (in(35, 38) || in(40, 44) || in(47, 50)) {
        return onlyEvent(1, 2); // a set's cross, grounded and mounting strikes
    }
    if (in(51, 56) || in(0xdb, 0xe0)) {
        return onlyEvent(2, 1);
    }
    if (animId == 0x68 || animId == 0x74 || animId == 0x78 || animId == 0x7a) {
        return onlyEvent(2, 10);
    }
    if (animId == 0xc1 || animId == 0xc2 || animId == 0xd4 || animId == 0x1ea || animId == 0x1f5) {
        return onlyEvent(1, 1);
    }
    if (animId == 0xfa) {
        RageCounts counts = onlyEvent(2, 1);
        counts.halvable = false;
        return counts;
    }
    if (animId == 0x1e4 || animId == 0x1e6 || animId == 0x1e8 || animId == 0x1ec || animId == 0x1ee ||
        animId == 0x1f0) {
        return onlyEvent(3, 2);
    }
    if (in(0x285, 0x28b) || in(0x28d, 0x293)) {
        return onlyEvent(1, 3);
    }
    return {};
}

// The repeat tracker: the gap that ends a run (ms), the hits of a run that set the flag, the throw bonus's step and
// its cap.
constexpr std::uint64_t kRepeatGapMs = 5000;
constexpr int kRepeatRun = 6;
constexpr float kThrowBonusStep = 0.27F;
constexpr float kThrowBonusMax = 2.0F;

} // namespace

RageCounts rageCounts(int animId) {
    for (const CountRow& row : kCounts) {
        if (row.animId == animId) {
            RageCounts counts;
            counts.event2 = row.event2;
            counts.event1 = row.event1;
            return counts;
        }
    }
    return otherAttack(animId);
}

int awardPoints(int count, int value, bool blocked) {
    const int points = count * value;
    return blocked ? points >> 1 : points;
}

int awardHitRage(RageMeter& rage, int animId, bool blocked, const CombatTuning& tuning, std::uint64_t nowMs,
                 const RepeatTracker& tracker, bool throwing) {
    const RageCounts counts = rageCounts(animId);
    const RageGain gain{.halved = tracker.halved(), .stateMultiplier = throwing ? tracker.bonus() : 1.0F};
    // A throw's two awards, each times the bonus its grab strikes built.
    if (animId >= anim_id::kThrow1Front && animId <= anim_id::kThrow2Left) {
        int added = 0;
        for (const int points : kThrowAwardPoints) {
            added += rage.add(static_cast<float>(blocked ? points >> 1 : points), tuning, nowMs, gain);
        }
        return added;
    }
    // Each award goes through the formula on its own, so each is truncated on its own.
    const bool halve = blocked && counts.halvable;
    const std::array<std::pair<int, int>, 5> awards{{{counts.event2, kRageEvent2Points},
                                                     {counts.event1, kRageEvent1Points},
                                                     {counts.event0, kRageEvent0Points},
                                                     {counts.event3, kRageEvent3Points},
                                                     {counts.event5, kRageEvent5Points}}};
    int added = 0;
    for (const auto& [count, value] : awards) {
        if (count != 0) {
            added += rage.add(static_cast<float>(awardPoints(count, value, halve)), tuning, nowMs, gain);
        }
    }
    return added;
}

RepeatKind repeatKind(int animId) {
    switch (animId) {
    case 12:
    case 14:
    case 16:
    case 19:
    case 20:
    case 35:
    case 40:
    case 46:
        return RepeatKind::Square;
    case 11:
    case 13:
    case 15:
    case 17:
    case 18:
        return RepeatKind::Cross;
    case 219:
    case 221:
    case 223:
        return RepeatKind::Mount;
    case 51:
    case 53:
    case 55:
        return RepeatKind::Grab;
    default:
        return RepeatKind::Other;
    }
}

void RepeatTracker::note(int animId, std::uint64_t nowMs) {
    const RepeatKind kind = repeatKind(animId);
    m_halved = false;
    // A run ends with an other kind, a change of kind, or a gap over the hold.
    if (kind == RepeatKind::Other || kind != m_lastKind || nowMs > m_lastMs + kRepeatGapMs) {
        m_squareCount = 0;
        m_crossCount = 0;
    }
    if (kind == RepeatKind::Square || kind == RepeatKind::Cross) {
        int& counted = kind == RepeatKind::Square ? m_squareCount : m_crossCount;
        ++counted;
        if (counted >= kRepeatRun) {
            counted = kRepeatRun - 1;
            m_halved = true;
        }
    } else if (kind == RepeatKind::Mount || kind == RepeatKind::Grab) {
        m_bonus = std::min(kThrowBonusMax, m_bonus + kThrowBonusStep);
    }
    m_lastMs = nowMs;
    m_lastKind = kind;
}

int RepeatTracker::count(RepeatKind kind) const {
    if (kind == RepeatKind::Square) {
        return m_squareCount;
    }
    return kind == RepeatKind::Cross ? m_crossCount : 0;
}

} // namespace coney::combat
