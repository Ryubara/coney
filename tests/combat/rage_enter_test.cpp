// SPDX-License-Identifier: GPL-3.0-or-later
// Rage's start (Rage_Enter): which pair it breaks and when 643 plays (docs/research/combat-rage.md#rage-effects).
#include "combat/rage_enter.h"

#include <catch2/catch_test_macros.hpp>

using namespace coney::combat;

TEST_CASE("rage's start breaks a pair no held move keeps and plays 643 only when free", "[combat]") {
    // Expected: combat-rage.md#rage-effects, confirmed (code)
    // Free: no pair to break, the clip plays. An attack's end phase (0x4) is not busy (0xfc1fe7b).
    CHECK(rageEnterSteps({.paired = false, .held = 0, .downOrCuffed = false}).playClip);
    CHECK(rageEnterSteps({.paired = false, .held = 0x4, .downOrCuffed = false}).playClip);
    CHECK_FALSE(rageEnterSteps({.paired = false, .held = 0, .downOrCuffed = false}).breakPair);
    // Paired with no held bit of 0x2fefefff: the pair is broken, and then free, the clip plays.
    const RageEnterSteps broken = rageEnterSteps({.paired = true, .held = 0, .downOrCuffed = false});
    CHECK(broken.breakPair);
    CHECK(broken.playClip);
    // Paired while a move holds a bit of 0x2fefefff (the grab's 0x10): the pair stays, and still paired, no clip.
    const RageEnterSteps kept = rageEnterSteps({.paired = true, .held = 0x10, .downOrCuffed = false});
    CHECK_FALSE(kept.breakPair);
    CHECK_FALSE(kept.playClip);
    // Down or cuffed (0xe0000), or a held bit of 0xfc1fe7b (0x2000, the counter's): rage starts with no clip.
    CHECK_FALSE(rageEnterSteps({.paired = false, .held = 0, .downOrCuffed = true}).playClip);
    CHECK_FALSE(rageEnterSteps({.paired = false, .held = 0x2000, .downOrCuffed = false}).playClip);
    // 0x20000000 keeps a pair (0x2fefefff) but is not busy (0xfc1fe7b): out of a pair, the clip plays.
    CHECK(rageEnterSteps({.paired = false, .held = 0x20000000, .downOrCuffed = false}).playClip);
}
