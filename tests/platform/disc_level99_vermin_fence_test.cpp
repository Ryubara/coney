// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc how an AI runs at `level99` checkpoint 3's fence and roofs, as the original
// does. The level plays in the game session as `--play-level level99 --checkpoint 3` plays it
// (support/disc_session.h), the pad idle.
//
// - **The fence demonstration** (issue #232): after the scene `l99_c3`, `P3.EndChapterScene` sends Vermin with
//   `GoalMoveToFlag(Vermin, fVerminFencePoizo, 4, -1, -1, 0.5, 0, true)` from the street to (46.30, 20.22), a route of
//   one kind-8 (climb) link from node 205 (46.29, 25.29) to node 204 (46.29, 24.29) over the fence. In the original he
//   ran straight, entered the climb (route state 3) at y 26.6, was over in 38 updates and stopped at (46.31, 20.18)
//   (docs/research/ai-pathing.md, confirmed (runtime)). Vermin is a fast climber (`HuSetFastClimber`), so he starts
//   the climb from the run within 4.5 m of the climb's far node (`Route_TryClimbNear`,
//   docs/research/ai.md#route-follow).
// - **The roof run** (issue #104): `P3.Player2Jumps` sends him from `fVerminWait` to `fStop[3]` over the window gap
//   and `P3.FinishRun` on to `fStop[4]` over the second gap, both at gait 4; in the original he held the run gait at
//   7.80 m/s up to both take-offs (docs/research/scripting.md#level99-p2-vermin-fence, confirmed (runtime)).
//
// It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints positions only
// (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/goal.h"
#include "animation/anim_math.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/created_humans.h"

namespace {

// The fence order's target (the flag (47.30, 20.20) moved 1 m along -1 degrees) and its arrival radius, metres.
constexpr float kTargetX = 46.30F;
constexpr float kTargetY = 20.217F;
constexpr float kArrival = 0.5F;
// The climb link's two nodes (y 25.29 and 24.29) and the fence between them.
constexpr float kFarNodeY = 24.29F;
constexpr float kFenceY = 24.79F;
// Where a running climb may start: within 4.5 m of the far node, and not against the fence (the running climb's
// nearest face is (r1 + r2) x 0.4 away, 1.2 m for this fence's clips; the original's state 3 began 1.8 m out).
constexpr float kTakeOffFarthestY = kFarNodeY + 4.5F;
constexpr float kTakeOffNearestY = kFenceY + 0.8F;
// The climb's updates at most (38 in the original) and the whole order's (the scene's end to the arrival: about 23 m
// at 7.8 m/s, the climb and the start; 246 before the fix, with 33 updates run against the fence).
constexpr int kClimbMost = 50;
constexpr int kFenceRunMost = 180;
// How long the scene and the set-up before the order may take, and any one run, in frames.
constexpr std::uint64_t kOrderLimit = 6000;
constexpr int kRunBudget = 600;

// The roof run: `fVerminWait`, where Vermin waits for the player's window jump; the updates looked at before each
// take-off (the original ran at gait 4 and 7.80 m/s from 34 updates before the window's take-off and 18 before the
// second); how long both legs may take (the original's second take-off was 266 updates after the order).
constexpr coney::anim::Vec3 kWindowRunStart{62.8F, -3.3F, 4.3F};
constexpr std::size_t kRunUp = 12;
constexpr int kRoofBudget = 900;

// One update of a run.
struct Update {
    coney::human::Gait gait = coney::human::Gait::Standing;
    float speed = 0.0F;
    coney::anim::Vec3 feet{};
    bool climbing = false;
};

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

// What `human` is doing now.
Update sample(const coney::human::Human& human) {
    return Update{.gait = human.gait(),
                  .speed = human.speed(),
                  .feet = human.position(),
                  .climbing = human.traversal() == coney::human::Traversal::Climbing};
}

// The index of the first climbing update in `updates`, if any.
std::optional<std::size_t> firstClimb(const std::vector<Update>& updates) {
    const auto it = std::ranges::find_if(updates, [](const Update& u) { return u.climbing; });
    return it != updates.end() ? std::optional<std::size_t>(static_cast<std::size_t>(it - updates.begin()))
                               : std::nullopt;
}

// How many updates the climb from `start` lasts.
int climbLength(const std::vector<Update>& updates, std::size_t start) {
    int length = 0;
    for (std::size_t i = start; i < updates.size() && updates[i].climbing; ++i) {
        ++length;
    }
    return length;
}

// Checkpoint 3 started in `run`, its scene played out, until the fence order: Vermin's brain, or null.
coney::ai::Brain* untilFenceOrder(coney::test::DiscSession& run) {
    run.session().startAtLevel("level99", 3);
    REQUIRE(run.runUntilPlay());
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    if (play == nullptr) {
        return nullptr;
    }
    coney::ai::Brain* brain = nullptr;
    REQUIRE(run.runUntil(
        [&] {
            brain = brainNamed(*play, lastNamed(run.session().flow().humans(), "Vermin"));
            return brain != nullptr && movingToFlag(*brain);
        },
        kOrderLimit));
    return brain;
}

// The fence order run out in `run`, then half a second: Vermin's brain, or null.
coney::ai::Brain* afterFenceDemonstration(coney::test::DiscSession& run) {
    coney::ai::Brain* brain = untilFenceOrder(run);
    if (brain == nullptr) {
        return nullptr;
    }
    REQUIRE(run.runUntil([&] { return !movingToFlag(*brain); }, kRunBudget));
    constexpr std::uint64_t kSettle = 15;
    run.run(kSettle);
    return brain;
}

} // namespace

TEST_CASE("the disc's level99 checkpoint 3: Vermin climbs the fence from his run in his demonstration",
          "[disc][story][level99]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "0 stick left 0 0\n");
    coney::ai::Brain* brain = untilFenceOrder(run);
    if (brain == nullptr) {
        return;
    }
    const coney::anim::Vec3 start = brain->human().position();
    const float runSpeed = brain->human().speeds().run;

    // Every update until the order is done.
    std::vector<Update> updates;
    for (int frame = 0; frame < kRunBudget && movingToFlag(*brain); ++frame) {
        run.run(1);
        updates.push_back(sample(brain->human()));
    }
    const std::optional<std::size_t> climb = firstClimb(updates);
    REQUIRE(climb.has_value());
    REQUIRE(*climb > 0);
    if (!climb || *climb == 0) {
        return;
    }
    const Update takeOff = updates[*climb];
    const Update before = updates[*climb - 1];
    const int climbUpdates = climbLength(updates, *climb);
    const std::size_t off = *climb + static_cast<std::size_t>(climbUpdates);
    const coney::anim::Vec3 landed = off < updates.size() ? updates[off].feet : updates.back().feet;
    // The updates he spent on the street side within 0.8 m of the fence without climbing: running into it.
    const auto againstFence = std::ranges::count_if(
        updates, [](const Update& u) { return !u.climbing && u.feet.y > kFenceY && u.feet.y < kTakeOffNearestY; });
    const coney::anim::Vec3 feet = brain->human().position();
    const float fromTarget = std::hypot(feet.x - kTargetX, feet.y - kTargetY);
    std::printf("  level99 Vermin's fence: from (%.2f, %.2f) in %zu updates; the climb from (%.2f, %.2f) at %.2f m/s, "
                "%d updates, off it at (%.2f, %.2f); %td updates against the fence; ends %.2f m from the target\n",
                start.x, start.y, updates.size(), takeOff.feet.x, takeOff.feet.y, before.speed, climbUpdates, landed.x,
                landed.y, againstFence, fromTarget);

    // One climb, started from the run on the street side short of the fence, never run into it; over in about the
    // original's time, off it beyond the far node, and the flag reached.
    CHECK(start.y > kTakeOffFarthestY);
    CHECK(takeOff.feet.y <= kTakeOffFarthestY);
    CHECK(takeOff.feet.y >= kTakeOffNearestY);
    CHECK(before.speed >= runSpeed * 0.9F);
    CHECK(againstFence == 0);
    CHECK(climbUpdates <= kClimbMost);
    CHECK(std::ranges::count_if(updates, [](const Update& u) { return u.climbing; }) == climbUpdates);
    CHECK(landed.y < kFarNodeY);
    CHECK(static_cast<int>(updates.size()) <= kFenceRunMost);
    CHECK(fromTarget <= kArrival + 0.1F);
}

TEST_CASE("the disc's level99 checkpoint 3: Vermin runs up to both of his roof jumps", "[disc][story][level99]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "0 stick left 0 0\n");
    coney::ai::Brain* brain = afterFenceDemonstration(run);
    if (brain == nullptr) {
        return;
    }

    // Vermin where the window run starts, then the script's step after the player's window jump, which sends him to
    // fStop[3]; its arrival line's end runs P3.FinishRun, which sends him on to fStop[4].
    brain->human().place(kWindowRunStart, coney::human::headingOf(coney::anim::Vec3{-1.0F, 0.0F, 0.0F}));
    REQUIRE(run.session().flow().scripts().call("P3.Player2Jumps"));

    const float runSpeed = brain->human().speeds().run;
    std::vector<Update> updates;
    std::vector<std::size_t> takeOffs;
    bool wasAirborne = brain->human().airborne();
    for (int frame = 0; frame < kRoofBudget && takeOffs.size() < 2; ++frame) {
        run.run(1);
        updates.push_back(sample(brain->human()));
        if (brain->human().airborne() && !wasAirborne) {
            takeOffs.push_back(updates.size() - 1);
        }
        wasAirborne = brain->human().airborne();
    }
    std::printf("  level99 Vermin's roof run: run speed %.2f m/s, %zu take-offs\n", runSpeed, takeOffs.size());
    REQUIRE(takeOffs.size() == 2);
    for (const std::size_t at : takeOffs) {
        REQUIRE(at >= kRunUp);
        // The updates before the take-off (the one before it the jump's own): the run gait at the run's speed.
        int running = 0;
        float slowest = runSpeed;
        for (std::size_t i = at - kRunUp; i < at - 1; ++i) {
            running += updates[i].gait == coney::human::Gait::Run ? 1 : 0;
            slowest = std::min(slowest, updates[i].speed);
        }
        const coney::anim::Vec3 feet = updates[at].feet;
        std::printf("    take-off at (%.2f, %.2f, %.2f): %d of %zu updates before it at the run, slowest %.2f m/s\n",
                    feet.x, feet.y, feet.z, running, kRunUp - 1, slowest);
        CHECK(running == static_cast<int>(kRunUp - 1));
        CHECK(slowest >= runSpeed * 0.95F);
    }
}
