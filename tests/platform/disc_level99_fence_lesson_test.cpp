// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc which way Vermin and the player face in `level99`'s fence lesson
// (docs/research/scripting.md#level99-p2-vermin-fence, item 43; issue #105). `P3.WoodenFences` (the player entering
// `vAlmostDone`) teleports Vermin to `fStop[5]` with the flag's heading, 273: a flag's heading turns clockwise seen
// from above (docs/research/flags.md#position), so he faces -x, towards the locked camera, and his `point_left` points
// at the fences on his left. The player is put at `fBreakFencePoizo` (184), facing the fences (-y). After
// `P3.BreakFence` Vermin walks to `fStop[6]` and turns to its 139, facing the fence `dyn_door_fence02` (south-east).
// The level plays in the game session as `--play-level level99 --checkpoint 3` plays it (support/disc_session.h); the
// test calls `P3.WoodenFences` itself once the chapter's scene is skipped, as the hand-over test calls the course's
// end. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts and
// headings only (LEGAL.md).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <optional>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/lua_value.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/created_humans.h"

namespace {

// The pad: cross at 100 skips the chapter's scene `l99_c3`, then nothing.
constexpr std::string_view kPad = "100 tap cross\n";
// The frame of play `P3.WoodenFences` is called on, the scene long over, and the updates after it Vermin is looked at.
constexpr std::uint64_t kFencesAt = 400;
constexpr std::uint64_t kSettle = 3;
// The longest the point, the 3 s before `P3.BreakFence` and the walk to `fStop[6]` may take, in frames.
constexpr int kWalkLimit = 900;
// How close each heading must be: the teleport's exactly (5 degrees), the walk's turn within the face-flag test (15).
constexpr float kTeleportSlack = 5.0F;
constexpr float kTurnSlack = 15.0F;
// `fStop[6]`, and how near Vermin must stand to count as there.
constexpr float kStop6X = 21.4F;
constexpr float kStop6Y = -4.1F;
constexpr float kStopReach = 0.6F;

// Degrees to radians.
float radians(float degrees) { return degrees * std::numbers::pi_v<float> / 180.0F; }

// How far `human` faces from the flags' heading `clockwiseDegrees`, in degrees.
float offFlag(const coney::human::Human& human, float clockwiseDegrees) {
    // A flag's clockwise heading h faces (sin h, cos h); Coney's humans face human::facing() of their own heading.
    const coney::anim::Vec3 ahead = coney::human::facing(human.heading());
    const float want = radians(clockwiseDegrees);
    const float dot = (ahead.x * std::sin(want)) + (ahead.y * std::cos(want));
    return std::acos(std::fmin(1.0F, std::fmax(-1.0F, dot))) * 180.0F / std::numbers::pi_v<float>;
}

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

} // namespace

TEST_CASE("the disc's level99: in the fence lesson Vermin faces the camera, then fStop[6]'s heading at the fence",
          "[disc][story][fence]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level99", 3);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kFencesAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    coney::script::ScriptSystem& scripts = run.session().flow().scripts();
    coney::ai::Brain* vermin = brainNamed(*play, lastNamed(run.session().flow().humans(), "Vermin"));
    REQUIRE(vermin != nullptr);

    // The player enters vAlmostDone: P3.WoodenFences(box, player).
    const std::array<coney::script::Value, 2> args{coney::script::Value(0.0),
                                                   coney::script::Value(play->fighters().playerBrain().handle())};
    REQUIRE(scripts.call("P3.WoodenFences", args));
    run.run(kSettle);
    const float verminAtPoint = offFlag(vermin->human(), 273.0F);
    const float playerAtFences = offFlag(play->fighters().playerBrain().human(), 184.0F);

    // P3.ShowFence, P3.BreakFence 3 s later, then the walk to fStop[6] and the turn to its heading.
    const auto there = [vermin] {
        const coney::anim::Vec3 feet = vermin->human().position();
        return std::hypot(feet.x - kStop6X, feet.y - kStop6Y) <= kStopReach && vermin->goalCount() == 0;
    };
    const bool arrived = run.runUntil(there, kWalkLimit);
    run.run(kSettle);
    const float verminAtStop = offFlag(vermin->human(), 139.0F);
    std::printf("  level99 fence lesson: Vermin %.1f deg off fStop[5]'s 273, player %.1f off 184; Vermin at fStop[6] "
                "%d, %.1f deg off its 139\n",
                static_cast<double>(verminAtPoint), static_cast<double>(playerAtFences), arrived ? 1 : 0,
                static_cast<double>(verminAtStop));
    CHECK(verminAtPoint <= kTeleportSlack);
    CHECK(playerAtFences <= kTeleportSlack);
    CHECK(arrived);
    CHECK(verminAtStop <= kTurnSlack);
}
