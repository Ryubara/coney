// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that an AI human walks `level99`'s gravel heap as the path data routes it:
// STORY from boot reaches level99 as the player plays it (tests/support/story_new_profile.txt through the shared
// start-up flow and level loader, tests/support/disc_play_fixtures.h); then one of the tutorial's bums is put at the
// start of the heap's kind-1 walk leg, (-271.7, 129.4), and sent as `GoalMoveToFlag` would to (-285.2, 124.2), the
// point a lesson-2 bum was walking to when he stuck at the heap's foot. His route climbs the heap to the lesson-2
// platform at (-277.7, 132.0, 3.1) and down the far side, beside a face too steep for floor (n.z 0.53,
// docs/research/characters.md#slopes). It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise; it prints positions only (LEGAL.md).
//
// It failed before the fix of 2026-10-11: the bum stopped for good at (-275.4, 131.6), at the face's foot. Coney's
// push-out left his sphere 0.01 m off the face each update, so the face was never a fraction-0 contact he slides along,
// and his move fell under the stuck test (0.2 m in 60 updates). A face within the radius and that clearance now counts
// as touched (docs/research/characters.md, "Coney's implementation").

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/move_to_flag_goal.h"
#include "animation/anim_math.h"
#include "gamemodes/gameplay_mode.h"
#include "human/human.h"
#include "platform/play_level_mode.h"
#include "world_objects/flags.h"

#include "support/disc_play_fixtures.h"

namespace {

// The walk leg's start, the platform it climbs to (the path data's nodes) and where the walk goes.
constexpr coney::anim::Vec3 kLegStart{-271.7F, 129.4F, 0.3F};
constexpr coney::anim::Vec3 kPlatform{-277.7F, 132.0F, 3.12F};
constexpr coney::anim::Vec3 kDestination{-285.2F, 124.2F, 0.3F};
// The arrival radius of the stuck bum's order, metres.
constexpr float kArrival = 2.0F;
// How long the walk may take, frames: about 17 m of route at a bum's 0.557 m/s walk is 31 s on the flat; with the
// slopes' slowing and the slide along the face's foot, a minute.
constexpr int kWalkBudget = 1800;
// The walk counts as over the platform within this of its node across the ground, metres.
constexpr float kOverPlatform = 1.0F;

// The one flag the test's order names: standing at the destination.
class DestinationFlag final : public coney::ai::FlagServices {
  public:
    [[nodiscard]] std::optional<coney::world_objects::Placement> flag(double /*handle*/) const override {
        return coney::world_objects::Placement{.position = {kDestination.x, kDestination.y, kDestination.z},
                                               .headingDegrees = 0.0F};
    }
};

// The distance across the ground between `a` and `b`.
float across(coney::anim::Vec3 a, coney::anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

} // namespace

TEST_CASE("the disc's level99: a bum walks over the gravel heap by its walk leg and the lesson-2 platform",
          "[disc][story]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    std::ifstream file(std::string(CONEY_TEST_SUPPORT_DIR) + "/story_new_profile.txt");
    REQUIRE(file.good());
    std::stringstream script;
    script << file.rdbuf();
    coney::test::DiscGame game(*wad, script.str());
    // Through the menus into level99, as the player gets there.
    game.run(460);
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    auto* play = dynamic_cast<coney::platform::PlayLevelMode*>(game.flow().gameplay().level());
    REQUIRE(play != nullptr);
    if (play == nullptr) {
        return;
    }
    REQUIRE(play->sceneName() == "level99");

    // The first bum (a civilian's brain: the tutorial's bums), switched on and given the walk (gait 2) as lesson 2's
    // script does (`BrDead(bum, 0)`, then `GoalMoveToFlag`), put at the leg's start facing the platform.
    coney::ai::Brains& brains = play->fighters().brains();
    coney::ai::Brain* found = nullptr;
    for (std::size_t i = 0; i < brains.size() && found == nullptr; ++i) {
        if (brains.at(i).type() == coney::ai::BrainType::Civilian) {
            found = &brains.at(i);
        }
    }
    REQUIRE(found != nullptr);
    if (found == nullptr) {
        return;
    }
    coney::ai::Brain& bum = *found;
    bum.setDead(false);
    bum.human().place(kLegStart, std::atan2(-(kPlatform.x - kLegStart.x), kPlatform.y - kLegStart.y));
    bum.clearGoals();
    DestinationFlag flags;
    REQUIRE(bum.pushGoal(std::make_unique<coney::ai::MoveToFlagGoal>(
        coney::ai::MoveToFlagOrder{.flag = 1, .gait = 2, .radius = kArrival}, flags)));

    // Until the goal is done, watching how high the walk goes and how near the platform it passes.
    float highest = kLegStart.z;
    float nearestPlatform = across(kLegStart, kPlatform);
    int frames = 0;
    for (; frames < kWalkBudget; ++frames) {
        game.run(1);
        const coney::anim::Vec3 feet = bum.human().position();
        highest = std::max(highest, feet.z);
        nearestPlatform = std::min(nearestPlatform, across(feet, kPlatform));
        const coney::ai::Goal* top = bum.topGoal();
        if (top == nullptr || top->type() != coney::ai::GoalType::MoveToFlag) {
            break;
        }
    }
    const coney::anim::Vec3 feet = bum.human().position();
    std::printf("  level99 heap: %d frames, at (%.2f, %.2f, %.2f), highest z %.2f, %.2f m from the platform\n", frames,
                feet.x, feet.y, feet.z, highest, nearestPlatform);
    CHECK(frames < kWalkBudget);
    CHECK(across(feet, kDestination) <= kArrival + 0.05F);
    CHECK(highest > kPlatform.z - 0.3F);
    CHECK(nearestPlatform < kOverPlatform);
}
