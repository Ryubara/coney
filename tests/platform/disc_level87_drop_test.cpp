// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that level87 (mission 3) checkpoint 3's first paint can be had as the script
// means it (docs/research/scripting.md#level87-fixes, fix 4): the chapter's two early Destroyers hold $5 each
// (`EDConstructor`, `HuSetMoney`); beaten, each leaves a `dyn_money` pickup of his $5 (Human_DropCarried,
// docs/research/crimes.md#down-drop), player 1 walks over both, and the $10 buys a can of paint from a spray seller
// (docs/research/ai-scripting.md#dealer). The level plays in the game session as `--play-level level87 --checkpoint 3`
// plays it (support/disc_session.h); player 1 is put beside each Destroyer and seller in turn (the debug teleport
// stands for the walk) and the pad does the rest: square at the Destroyer, triangle at the seller. It runs only when
// the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts and dollars only
// (LEGAL.md).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/dealer_goal.h"
#include "ai/goal.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/place.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"
#include "warriors/inventory.h"
#include "world_objects/spawn_records.h"

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// The frame of play at which the opening scene is skipped, and the one the first action waits for.
constexpr std::uint64_t kSkipAt = 100;
constexpr std::uint64_t kSettleAt = 400;
// The early Destroyers' money (`EDConstructor`'s `HuSetMoney`), and the spray seller's price for a can
// (ai::dealTerms(), docs/research/ai-scripting.md#dealer).
constexpr int kDestroyerMoney = 5;
// The early Destroyers' character type (a level87 checkpoint 3 run finds its humans holding the $5; inferred, see
// docs/research/scripting.md#level87-fixes, fix 3).
constexpr int kEarlyDestroyerType = 194;
constexpr int kCanPrice = 5;
// How long a fight or a deal may take, frames.
constexpr std::uint64_t kFightFrames = 900;
constexpr std::uint64_t kDealFrames = 240;
// How far from a Destroyer or a seller player 1 is put, metres.
constexpr float kStandOff = 1.0F;

// One run of checkpoint 3 with player 1 driven through the live pad.
struct Chapter {
    coney::test::LivePad& pad;
    coney::test::DiscSession& run;

    // Plays `script` (frames counted from two frames on) and runs `frames` frames.
    void play(std::string_view script, std::uint64_t frames) {
        const std::uint64_t from = pad.nextFrame() + 2;
        std::string shifted;
        std::size_t start = 0;
        while (start < script.size()) {
            const std::size_t end = script.find('\n', start);
            const std::string_view line = script.substr(start, end - start);
            const std::size_t space = line.find(' ');
            shifted +=
                std::format("{} {}\n", from + std::stoull(std::string(line.substr(0, space))), line.substr(space + 1));
            start = end == std::string_view::npos ? script.size() : end + 1;
        }
        pad.play(shifted);
        run.run(frames + 2);
    }
    // The play mode.
    [[nodiscard]] coney::platform::PlayLevelMode& mode() {
        coney::platform::PlayLevelMode* play = run.session().play();
        REQUIRE(play != nullptr);
        return *play;
    }
    // Puts player 1 `kStandOff` metres from `at`, on the side he stands on, facing it.
    void faceFrom(coney::anim::Vec3 at) {
        const coney::anim::Vec3 feet = mode().playerFeet();
        float dx = feet.x - at.x;
        float dy = feet.y - at.y;
        const float length = std::hypot(dx, dy);
        dx = length > 0.0F ? dx / length : 1.0F;
        dy = length > 0.0F ? dy / length : 0.0F;
        // Heading 0 faces +y and turns counter-clockwise, so facing (-dx, -dy) is atan2(dx, -dy).
        mode().teleport(coney::human::Place{.name = "drop",
                                            .feet = {at.x + (dx * kStandOff), at.y + (dy * kStandOff), at.z},
                                            .headingDegrees = std::atan2(dx, -dy) * 180.0F / kPi});
    }
    // Player 1's count of item `item`.
    [[nodiscard]] int carried(int item) { return run.session().flow().state().player.inventory.count(0, item); }
    // Every `dyn_money` record ever made, taken or not.
    [[nodiscard]] std::vector<const coney::world_objects::SpawnRecord*> moneyMade() {
        std::vector<const coney::world_objects::SpawnRecord*> made;
        for (const coney::world_objects::SpawnRecord& record : run.session().flow().spawnRecords().all()) {
            if (record.typeName == "dyn_money") {
                made.push_back(&record);
            }
        }
        return made;
    }
};

} // namespace

TEST_CASE("the disc's level87 checkpoint 3: the beaten early Destroyers' $5 each buys a can of paint",
          "[disc][story][money]") {
    // Expected: scripting.md#level87-fixes (fix 4), inferred from the script (EDConstructor's HuSetMoney(v, 5));
    // crimes.md#down-drop, confirmed (code) at 0x00232c60 (an AI's whole money becomes a dyn_money pickup); the can's
    // price, ai-scripting.md#dealer, confirmed (code).
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::LivePad pad;
    pad.play(std::format("{} tap cross\n", kSkipAt));
    coney::test::DiscSession run(*wad, pad);
    run.session().startAtLevel("level87", 3);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kSettleAt));
    Chapter chapter{.pad = pad, .run = run};

    // The early Destroyers: the AI humans of their type holding the constructor's $5.
    std::vector<coney::human::Human*> destroyers;
    for (const coney::ai::AiHuman& made : chapter.mode().fighters().humans()) {
        const coney::ai::Brain* brain = made.human ? chapter.mode().fighters().brainOf(*made.human) : nullptr;
        if (!made.removed && brain != nullptr && brain->characterClass() == kEarlyDestroyerType &&
            made.human->script().money == kDestroyerMoney) {
            destroyers.push_back(made.human.get());
        }
    }
    std::printf("level87 drop: %zu early Destroyers hold $%d\n", destroyers.size(), kDestroyerMoney);
    REQUIRE(destroyers.size() == 2);
    // What player 1 gains from the two pickups: the crew fights beside him and may down another of the gang, whose
    // own drop he can walk over during the fight, so his whole money is not the measure. A pickup he already stands
    // on when its man goes down (the drop lies at the man's feet, a metre from where the fight puts him) is taken
    // at once, so his gain counts from before the fight for it, and from before the walk for one that lies.
    int taken = 0;

    // Each one beaten with square (his health first cut to one hit's worth, so the fight is short), then his
    // money walked over.
    for (coney::human::Human* destroyer : destroyers) {
        coney::ai::Brain* brain = chapter.mode().fighters().brainOf(*destroyer);
        REQUIRE(brain != nullptr);
        const std::size_t madeBefore = chapter.moneyMade().size();
        const int moneyBeforeFight = chapter.carried(coney::item::kMoney);
        const std::array<coney::script::Value, 2> health{coney::script::Value(brain->handle()),
                                                         coney::script::Value(1.0)};
        (void)run.session().flow().scripts().call("HuSetHealth", health);
        std::uint64_t spent = 0;
        while (!destroyer->knockedOut() && spent < kFightFrames) {
            chapter.faceFrom(destroyer->position());
            chapter.play("0 tap square\n", 30);
            spent += 32;
        }
        std::printf("level87 drop: Destroyer down after %llu frames\n", static_cast<unsigned long long>(spent));
        REQUIRE(destroyer->knockedOut());
        CHECK(destroyer->script().money == 0);
        // His money lies at his feet, half a metre up: one new pickup, which player 1 took already or walks onto.
        const std::vector<const coney::world_objects::SpawnRecord*> made = chapter.moneyMade();
        REQUIRE(made.size() == madeBefore + 1);
        const coney::world_objects::SpawnRecord& drop = *made.back();
        CHECK(drop.money == static_cast<std::uint32_t>(kDestroyerMoney));
        if (drop.removed) {
            taken += chapter.carried(coney::item::kMoney) - moneyBeforeFight;
        } else {
            const coney::anim::Vec3 cash{drop.position[0], drop.position[1], drop.position[2]};
            chapter.mode().teleport(coney::human::Place{
                .name = "cash", .feet = {cash.x, cash.y, chapter.mode().playerFeet().z}, .headingDegrees = 0.0F});
            const int beforeWalk = chapter.carried(coney::item::kMoney);
            chapter.play("0 tap square\n", 10);
            taken += chapter.carried(coney::item::kMoney) - beforeWalk;
        }
        CHECK(drop.removed);
    }
    const int moneyAfter = chapter.carried(coney::item::kMoney);
    std::printf("level87 drop: player 1 took $%d from the two pickups; money $%d\n", taken, moneyAfter);
    CHECK(taken >= 2 * kDestroyerMoney);

    // A spray seller offering a can: triangle beside him buys it.
    const int paintBefore = chapter.carried(coney::item::kSprayPaint);
    coney::ai::Brains& brains = chapter.mode().fighters().brains();
    for (std::size_t tries = 0; tries < 2 && chapter.carried(coney::item::kSprayPaint) == paintBefore; ++tries) {
        bool found = false;
        for (std::size_t i = 0; i < brains.size() && !found; ++i) {
            coney::ai::Brain& seller = brains.at(i);
            coney::ai::Goal* goal = seller.topGoal();
            if (goal == nullptr || goal->type() != coney::ai::GoalType::Dealer) {
                continue;
            }
            const auto& dealer = static_cast<const coney::ai::DealerGoal&>(*goal);
            const std::optional<coney::ai::DealTerms> terms = coney::ai::dealTerms(dealer.type());
            if (!terms || terms->item != coney::item::kSprayPaint) {
                continue;
            }
            found = true;
            chapter.faceFrom(seller.human().position());
            chapter.play("60 tap triangle\n", kDealFrames);
        }
        REQUIRE(found);
    }
    const int paintAfter = chapter.carried(coney::item::kSprayPaint);
    std::printf("level87 drop: spray paint %d, then %d; money $%d\n", paintBefore, paintAfter,
                chapter.carried(coney::item::kMoney));
    CHECK(paintAfter > paintBefore);
    CHECK(chapter.carried(coney::item::kMoney) <= moneyAfter - kCanPrice);
}
