// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/rage_awards.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/combat_tuning.h"
#include "combat/meters.h"

// The rage the player's hits give, as two awards truncated on their own, against the gains read at runtime
// (docs/research/combat.md#rage, docs/research/combat.md#being-hit-runtime).

using namespace coney::combat;

namespace {

// The rage a fresh player meter gains from one hit of `animId`.
int gainOf(int animId, bool blocked) {
    const CombatTuning tuning;
    RageMeter rage;
    const RepeatTracker tracker;
    return awardHitRage(rage, animId, blocked, tuning, 0, tracker);
}

} // namespace

TEST_CASE("each chain attack gives the rage seen at runtime", "[combat]") {
    struct Row {
        int animId;
        int gain;
    };
    // X1 5, S1 1, XX2 6, XS2 10, SX2 13, SS2 1, SSX3 15, SSS3 4, the moving attacks 1.
    const std::array<Row, 11> rows{{{.animId = 11, .gain = 5},
                                    {.animId = 12, .gain = 1},
                                    {.animId = 13, .gain = 6},
                                    {.animId = 14, .gain = 10},
                                    {.animId = 15, .gain = 13},
                                    {.animId = 16, .gain = 1},
                                    {.animId = 17, .gain = 15},
                                    {.animId = 19, .gain = 4},
                                    {.animId = 24, .gain = 1},
                                    {.animId = 76, .gain = 10},
                                    {.animId = 104, .gain = 1}}};
    for (const Row& row : rows) {
        CHECK(gainOf(row.animId, false) == row.gain);
    }
    // Ids with no award give nothing.
    CHECK(gainOf(51, false) == 0);
}

TEST_CASE("a blocked hit halves each award with a shift", "[combat]") {
    // S1 and SS2: 1 >> 1 = 0; SSX3: (3 >> 1) and (8 >> 1) give 1 + 5.
    CHECK(gainOf(12, true) == 0);
    CHECK(gainOf(16, true) == 0);
    CHECK(gainOf(17, true) == 6);
    CHECK(awardPoints(3, kRageEvent2Points, true) == 1);
    CHECK(awardPoints(2, kRageEvent1Points, true) == 4);
}

TEST_CASE("each award sets the hold, and rage on gives nothing", "[combat]") {
    const CombatTuning tuning;
    RageMeter rage;
    CHECK(awardHitRage(rage, 15, false, tuning, 1000, RepeatTracker{}) == 13);
    // Held until 6000 ms: no decay before it.
    rage.update(5990, tuning);
    CHECK(rage.value() == 13);
    rage.set(78);
    REQUIRE(rage.start(6000));
    CHECK(awardHitRage(rage, 15, false, tuning, 6000, RepeatTracker{}) == 0);
}

TEST_CASE("the sixth hit of a kind in a row within 5 s halves the rage of the later ones", "[combat]") {
    // X1 every second, as at runtime: 5 rage each for six hits, then 2 (trunc(5.76 × 0.5)).
    const CombatTuning tuning;
    RageMeter rage;
    RepeatTracker tracker;
    std::array<int, 8> gains{};
    for (std::size_t hit = 0; hit < gains.size(); ++hit) {
        const std::uint64_t now = 1000 * (hit + 1);
        gains.at(hit) = awardHitRage(rage, 11, false, tuning, now, tracker);
        tracker.note(11, now);
    }
    CHECK(gains == std::array<int, 8>{5, 5, 5, 5, 5, 5, 2, 2});
    CHECK(tracker.count(RepeatKind::Cross) == 5);
    // A square-kind hit ends the run: full rage again.
    tracker.note(12, 9000);
    CHECK_FALSE(tracker.halved());
    CHECK(tracker.count(RepeatKind::Cross) == 0);
    CHECK(tracker.count(RepeatKind::Square) == 1);
}

TEST_CASE("a gap over 5 s restarts the run", "[combat]") {
    RepeatTracker tracker;
    for (std::uint64_t hit = 0; hit < 8; ++hit) {
        tracker.note(11, 5170 * hit);
        CHECK(tracker.count(RepeatKind::Cross) == 1);
        CHECK_FALSE(tracker.halved());
    }
}

TEST_CASE("each grab strike raises the throw bonus by 0.27, up to 2.0", "[combat]") {
    RepeatTracker tracker;
    tracker.note(51, 0);
    tracker.note(53, 100);
    CHECK(tracker.bonus() == Catch::Approx(1.54F));
    for (std::uint64_t hit = 0; hit < 5; ++hit) {
        tracker.note(55, 200 + hit);
    }
    CHECK(tracker.bonus() == Catch::Approx(2.0F));
    CHECK(repeatKind(219) == RepeatKind::Mount);
    CHECK(repeatKind(147) == RepeatKind::Other);
    tracker.resetBonus();
    CHECK(tracker.bonus() == Catch::Approx(1.0F));
}
