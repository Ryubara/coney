// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that Vermin climbs his fence in `level99` checkpoint 3's demonstration, as the
// original does (issue #232): after the scene `l99_c3`, `P3.EndChapterScene` sends him with
// `GoalMoveToFlag(Vermin, fVerminFencePoizo, 4, -1, -1, 0.5, 0, true)` from the street to (46.30, 20.22), a route of
// one kind-8 (climb) link from node 205 (46.29, 25.29) to node 204 (46.29, 24.29) over the fence. In the original he
// ran straight, started the climb at y 26.6, was over in 38 updates, left it at y 22.3 and stopped at (46.31, 20.18)
// (docs/research/ai-pathing.md, confirmed (runtime); docs/research/scripting.md#level99-checkpoints step 3.2). The
// level plays in the game session as `--play-level level99 --checkpoint 3` plays it (support/disc_session.h), the
// pad idle. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints
// positions only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/goal.h"
#include "animation/anim_math.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/created_humans.h"

namespace {

// The goal's target (the flag (47.30, 20.20) moved 1 m along -1 degrees) and its arrival radius, metres.
constexpr float kTargetX = 46.30F;
constexpr float kTargetY = 20.217F;
constexpr float kArrival = 0.5F;
// The fence line between the climb link's two nodes (y 25.29 and 24.29).
constexpr float kFenceY = 24.79F;
// Where the climb may start: on the street side of the fence, no farther back than the original's 26.6 plus the
// climb's own reach, and close to the link's line in x.
constexpr float kTakeOffFarthestY = 27.6F;
constexpr float kTakeOffNearestY = 25.0F;
constexpr float kLinkX = 46.29F;
constexpr float kOffLink = 1.0F;
// The original's landing side: off the climb at y 22.3; anything past the far node counts.
constexpr float kLandedBeforeY = 24.29F;
// How long the scene and the set-up before the order may take, and the run itself (about 23 m at the run's 7.8 m/s
// and a 38-update climb in the original, under 5 s), in frames.
constexpr std::uint64_t kOrderLimit = 6000;
constexpr int kRunBudget = 600;

// The handle of the last human the scripts made with `name`, or 0.
double lastNamed(const coney::CreatedHumans& humans, std::string_view name) {
    double handle = 0.0;
    for (const coney::HumanCreation& human : humans.all()) {
        if (human.name == name) {
            handle = human.handle;
        }
    }
    return handle;
}

// The brain the scripts name `handle`, or null.
coney::ai::Brain* brainNamed(coney::platform::PlayLevelMode& play, double handle) {
    coney::ai::Brains& brains = play.fighters().brains();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        if (brains.at(i).handle() == handle) {
            return &brains.at(i);
        }
    }
    return nullptr;
}

// Whether `brain` is on its way to a flag (the fence order's goal on top).
bool movingToFlag(const coney::ai::Brain& brain) {
    const coney::ai::Goal* top = brain.topGoal();
    return top != nullptr && top->type() == coney::ai::GoalType::MoveToFlag;
}

} // namespace

TEST_CASE("the disc's level99 checkpoint 3: Vermin climbs the fence in his demonstration",
          "[disc][story][level99]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }

    // Checkpoint 3 as --play-level starts it, the pad idle; the scene `l99_c3` plays out to its end.
    coney::test::DiscSession run(*wad, "0 stick left 0 0\n");
    run.session().startAtLevel("level99", 3);
    REQUIRE(run.runUntilPlay());
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    if (play == nullptr) {
        return;
    }
    const double vermin = lastNamed(run.session().flow().humans(), "Vermin");
    REQUIRE(vermin != 0.0);

    // The scene's end sends him to the flag.
    coney::ai::Brain* brain = nullptr;
    REQUIRE(run.runUntil(
        [&] {
            brain = brainNamed(*play, lastNamed(run.session().flow().humans(), "Vermin"));
            return brain != nullptr && movingToFlag(*brain);
        },
        kOrderLimit));
    if (brain == nullptr) {
        return;
    }
    const coney::anim::Vec3 start = brain->human().position();

    // Every update until the goal is done: where the climb starts, where he is when it ends, how far he got.
    std::optional<coney::anim::Vec3> takeOff;
    std::optional<coney::anim::Vec3> landed;
    int climbs = 0;
    int frames = 0;
    for (; frames < kRunBudget && movingToFlag(*brain); ++frames) {
        const bool wasClimbing = brain->human().climb().has_value();
        run.run(1);
        const bool climbing = brain->human().climb().has_value();
        const coney::anim::Vec3 feet = brain->human().position();
        if (climbing && !wasClimbing) {
            ++climbs;
            if (!takeOff) {
                takeOff = feet;
            }
        }
        if (!climbing && wasClimbing && !landed) {
            landed = feet;
        }
    }
    const coney::anim::Vec3 feet = brain->human().position();
    const float fromTarget = std::hypot(feet.x - kTargetX, feet.y - kTargetY);
    std::printf("  level99 Vermin's fence: from (%.2f, %.2f) in %d frames, %d climbs, take-off %s, off the climb %s, "
                "ends at (%.2f, %.2f, %.2f), %.2f m from the target\n",
                start.x, start.y, frames, climbs,
                takeOff ? std::format("({:.2f}, {:.2f})", takeOff->x, takeOff->y).c_str() : "none",
                landed ? std::format("({:.2f}, {:.2f})", landed->x, landed->y).c_str() : "none", feet.x, feet.y,
                feet.z, fromTarget);

    // He starts the street side of the fence, climbs once, from near the link, and comes off it beyond the fence.
    CHECK(start.y > kTakeOffFarthestY);
    REQUIRE(takeOff.has_value());
    REQUIRE(landed.has_value());
    if (!takeOff || !landed) {
        return;
    }
    CHECK(climbs == 1);
    CHECK(takeOff->y <= kTakeOffFarthestY);
    CHECK(takeOff->y >= kTakeOffNearestY);
    CHECK(std::fabs(takeOff->x - kLinkX) < kOffLink);
    CHECK(landed->y < kLandedBeforeY);
    CHECK(feet.y < kFenceY);
    // And he arrives at the flag in time.
    CHECK(frames < kRunBudget);
    CHECK(fromTarget <= kArrival + 0.1F);
}
