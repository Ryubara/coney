// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that circle + triangle's spray spends player 1's spray paint in a level as
// `coney --play-level level99` plays it (support/disc_session.h): given 3 paint and an empty hand, one press plays 664
// once and leaves 2 paint; given 3 paint and a bat in hand, the press plays no 664 and spends nothing
// (docs/research/combat/index.md#run-attacks). It runs only when the environment variable CONEY_DISC names the disc and
// skips otherwise; it prints counts only (LEGAL.md).

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ids.h"
#include "fileio/wad.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/inventory.h"

namespace {

namespace id = coney::combat::anim_id;

// The play frame player 1 is given the paint (and the bat) on, after the intro scene is skipped with cross at 100.
constexpr std::uint64_t kSetUpAt = 250;
// The play frame circle + triangle goes down, and the frame the run ends on.
constexpr std::uint64_t kPressAt = 300;
constexpr std::uint64_t kEndAt = 380;
// The paint player 1 starts the press with.
constexpr int kPaint = 3;
// A bat's anim set and an object number for it (as disc_bat_cross_test.cpp hands one over).
constexpr int kBatSet = 3;
constexpr double kBatObject = 9999.0;

// What one press did: the sprays started (664) and the paint left.
struct SprayResult {
    int sprays = 0;
    int paintLeft = 0;
};

// Plays level99 to kSetUpAt, gives player 1 kPaint spray paint (and the bat with `bat`), presses circle + triangle at
// kPressAt and counts what followed.
SprayResult pressWithPaint(const coney::io::Wad& wad, bool bat) {
    const std::string script = "100 tap cross\n" + std::to_string(kPressAt) + " press circle triangle\n" +
                               std::to_string(kPressAt + 4) + " release circle triangle\n";
    coney::test::DiscSession run(wad, script);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kSetUpAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    coney::Inventory& inventory = run.session().flow().state().player.inventory;
    inventory.set(0, coney::item::kSprayPaint, kPaint);
    auto& player = const_cast<coney::human::Player&>(play->player());
    if (bat) {
        player.human().script().heldObject = kBatObject;
        player.human().script().heldObjectName = "dyn_bat_tuff";
        player.human().fighter().setAnimSet(kBatSet);
    }
    SprayResult result;
    for (std::uint64_t frame = run.playFrames(); frame < kEndAt; ++frame) {
        run.run(1);
        result.sprays += player.human().fighter().last().startAnim == id::kSpraySpecial ? 1 : 0;
    }
    result.paintLeft = inventory.count(0, coney::item::kSprayPaint);
    return result;
}

} // namespace

TEST_CASE("the disc's level99: circle + triangle with 3 paint sprays once and leaves 2; with a bat it does nothing",
          "[disc][story][combat]") {
    // Expected: combat/index.md#run-attacks, confirmed (runtime)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // The original at runtime: 3 paint, one press, 664 and 2 paint (run r-20261025-circle-triangle-spray).
    const SprayResult empty = pressWithPaint(*wad, false);
    std::printf("spray, empty hand: %d sprays, %d paint left\n", empty.sprays, empty.paintLeft);
    CHECK(empty.sprays == 1);
    CHECK(empty.paintLeft == kPaint - 1);

    // A player holding an object is refused (Player_Special's Human_IsHoldingObject): no spray, no paint spent.
    const SprayResult armed = pressWithPaint(*wad, true);
    std::printf("spray, bat in hand: %d sprays, %d paint left\n", armed.sprays, armed.paintLeft);
    CHECK(armed.sprays == 0);
    CHECK(armed.paintLeft == kPaint);
}
