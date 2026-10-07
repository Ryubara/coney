// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/rage_awards.h"

#include <algorithm>
#include <array>

#include "combat/anim_ids.h"

namespace coney::combat {

namespace {

// One attack's counts.
struct CountRow {
    int animId;
    RageCounts counts;
};

// The research's table (docs/research/combat.md#rage): the chain and moving attacks, the knife's, baton's and bat's
// attacks and the strikes and moving attacks of event 1 × 1; then Coney's two grab moves.
constexpr std::array<CountRow, 38> kCounts{{
    {.animId = 11, .counts = {.event2 = 0, .event1 = 1}},  {.animId = 12, .counts = {.event2 = 1, .event1 = 0}},
    {.animId = 13, .counts = {.event2 = 1, .event1 = 1}},  {.animId = 14, .counts = {.event2 = 4, .event1 = 1}},
    {.animId = 15, .counts = {.event2 = 2, .event1 = 2}},  {.animId = 16, .counts = {.event2 = 1, .event1 = 0}},
    {.animId = 17, .counts = {.event2 = 3, .event1 = 2}},  {.animId = 18, .counts = {.event2 = 3, .event1 = 2}},
    {.animId = 19, .counts = {.event2 = 3, .event1 = 0}},  {.animId = 20, .counts = {.event2 = 3, .event1 = 0}},
    {.animId = 21, .counts = {.event2 = 1, .event1 = 0}},  {.animId = 22, .counts = {.event2 = 1, .event1 = 0}},
    {.animId = 23, .counts = {.event2 = 1, .event1 = 0}},  {.animId = 24, .counts = {.event2 = 1, .event1 = 0}},
    {.animId = 34, .counts = {.event2 = 2, .event1 = 0}},  {.animId = 35, .counts = {.event2 = 0, .event1 = 2}},
    {.animId = 36, .counts = {.event2 = 0, .event1 = 2}},  {.animId = 37, .counts = {.event2 = 0, .event1 = 2}},
    {.animId = 38, .counts = {.event2 = 0, .event1 = 2}},  {.animId = 39, .counts = {.event2 = 2, .event1 = 0}},
    {.animId = 40, .counts = {.event2 = 0, .event1 = 2}},  {.animId = 41, .counts = {.event2 = 0, .event1 = 2}},
    {.animId = 42, .counts = {.event2 = 0, .event1 = 2}},  {.animId = 43, .counts = {.event2 = 0, .event1 = 2}},
    {.animId = 44, .counts = {.event2 = 0, .event1 = 2}},  {.animId = 45, .counts = {.event2 = 2, .event1 = 0}},
    {.animId = 46, .counts = {.event2 = 2, .event1 = 0}},  {.animId = 47, .counts = {.event2 = 0, .event1 = 2}},
    {.animId = 48, .counts = {.event2 = 0, .event1 = 2}},  {.animId = 49, .counts = {.event2 = 0, .event1 = 2}},
    {.animId = 50, .counts = {.event2 = 0, .event1 = 2}},  {.animId = 193, .counts = {.event2 = 0, .event1 = 1}},
    {.animId = 194, .counts = {.event2 = 0, .event1 = 1}}, {.animId = 212, .counts = {.event2 = 0, .event1 = 1}},
    {.animId = 490, .counts = {.event2 = 0, .event1 = 1}}, {.animId = 501, .counts = {.event2 = 0, .event1 = 1}},
    {.animId = 76, .counts = {.event2 = 7, .event1 = 0}},  {.animId = 104, .counts = {.event2 = 1, .event1 = 0}},
}};

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
            return row.counts;
        }
    }
    return {};
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
    int added =
        rage.add(static_cast<float>(awardPoints(counts.event2, kRageEvent2Points, blocked)), tuning, nowMs, gain);
    added += rage.add(static_cast<float>(awardPoints(counts.event1, kRageEvent1Points, blocked)), tuning, nowMs, gain);
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
