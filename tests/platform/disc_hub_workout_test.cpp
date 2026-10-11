// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the clubhouse gym's workouts (level95, docs/research/workouts.md) play
// through the pad as a player plays them: level95 starts at checkpoint 2 in the game session as `--play-level` starts
// it (support/disc_session.h), and a driver that reads the game walks player 1 to a station with the left stick at a
// partial deflection along the level's paths, taps triangle beside its prompt and then plays the station. It runs only
// when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brains.h"
#include "ai/route_planner.h"
#include "core/input_script.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"
#include "world_objects/flags.h"

namespace {

using coney::anim::Vec3;

// The stations' prompt flags (fStartWorkout[k]) and workout flags (fWorkout[k]) as the research gives them, metres.
struct Station {
    std::string_view name;
    Vec3 prompt;
    Vec3 workout;
};
constexpr std::array<Station, 4> kStations{{
    {"heavy bag", {-174.8F, 106.0F, -197.5F}, {-175.5F, 106.0F, -197.4F}},
    {"chin-ups", {-181.0F, 106.4F, -197.5F}, {-181.0F, 105.7F, -197.5F}},
    {"push-ups", {-174.0F, 109.6F, -197.4F}, {-174.0F, 109.0F, -197.4F}},
    {"sit-ups", {-177.2F, 109.5F, -197.4F}, {-177.2F, 108.7F, -197.4F}},
}};

// How far the stick is pushed while walking (a walk, below the run's 0.95), and how near the driver goes to a
// waypoint before taking the next, and to the prompt before it stops: inside the context action's 1.8 m.
constexpr int kWalkPercent = 60;
constexpr float kWaypointReach = 0.6F;
constexpr float kPromptReach = 1.0F;
// Frames he moves less than kStillDistance (m) in a row, after which the driver plans its route again.
constexpr int kStillFrames = 30;
constexpr float kStillDistance = 0.02F;
// The gym's stairs, from the main room (z -194.3) down to the gym's floor (z -197.5), as the level's path data has
// them; a player above kGymFloorAbove is in the main room.
constexpr Vec3 kStairsTop{-164.02F, 97.34F, -194.30F};
constexpr Vec3 kStairsFoot{-163.46F, 91.59F, -197.47F};
constexpr float kGymFloorAbove = -196.0F;
constexpr float kDegreesPerRadian = 57.29578F;

// The distance in plan between two points.
float planDistance(Vec3 a, Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// How many of `lines` from `from` on contain `text`.
std::size_t countFrom(const std::vector<std::string>& lines, std::size_t from, std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = from; i < lines.size(); ++i) {
        count += lines[i].find(text) != std::string::npos ? 1 : 0;
    }
    return count;
}

// level95 at checkpoint 2 with a pad the driver steers frame by frame, the scripts' calls traced.
class Gym {
  public:
    explicit Gym(const coney::io::Wad& wad) : m_run(wad, m_pad), m_scripts(m_run.session().flow().scripts()) {
        m_run.session().startAtLevel("level95", 2, [this] {
            m_scripts.traceCalls([this](std::string_view line) { m_trace.emplace_back(line); });
        });
    }

    // Runs until the level plays; whether it does.
    bool start() { return m_run.runUntilPlay(); }

    // The play mode (null before the level plays) and player 1's human.
    coney::platform::PlayLevelMode* play() { return m_run.session().play(); }
    const coney::human::Human& player() { return play()->player().human(); }
    // Whether the pad drives player 1 now (the scripts hold him otherwise).
    bool padControlled() { return play()->player().padControlled(); }

    // One frame with the left stick at (x, y) per cent and `taps` (input script button names, space-separated)
    // pressed for this frame.
    void frame(int x = 0, int y = 0, std::string_view taps = {}) {
        const std::uint64_t at = m_pad.nextFrame();
        std::string script = std::format("{} stick left {} {}\n", at, x, y);
        if (!taps.empty()) {
            script += std::format("{} tap {}\n", at, taps);
        }
        m_pad.play(script);
        m_run.run(1);
    }
    // `frames` frames with the pad at rest.
    void idle(int frames) {
        for (int i = 0; i < frames; ++i) {
            frame();
        }
    }
    // Idles until `done` holds, at most `limit` frames; whether it did.
    bool idleUntil(const std::function<bool()>& done, int limit) {
        for (int i = 0; i < limit && !done(); ++i) {
            frame();
        }
        return done();
    }

    // Walks player 1 to within `reach` of `point` with the stick at kWalkPercent, along the level's paths: waits while
    // the scripts hold him, plans a route from where he stands (again whenever he has not moved for kStillFrames) and
    // steers at each waypoint through the camera as it faces now. Whether he got there within `limit` frames.
    bool walkTo(Vec3 point, float reach, int limit) {
        std::vector<Vec3> waypoints;
        bool planned = false;
        std::size_t next = 0;
        Vec3 lastPlace = player().position();
        int still = 0;
        for (int i = 0; i < limit; ++i) {
            if (planDistance(player().position(), point) <= reach) {
                return true;
            }
            if (!padControlled()) {
                frame();
                planned = false;
                continue;
            }
            still = planDistance(player().position(), lastPlace) < kStillDistance ? still + 1 : 0;
            lastPlace = player().position();
            if (!planned || still >= kStillFrames) {
                waypoints = routeTo(point);
                next = 0;
                planned = true;
                still = 0;
            }
            while (next < waypoints.size() && planDistance(player().position(), waypoints[next]) <= kWaypointReach) {
                ++next;
            }
            const Vec3 aim = next < waypoints.size() ? waypoints[next] : point;
            const Vec3 forward = player().record().cameraForward;
            const coney::SteerView view{.x = player().position().x,
                                        .y = player().position().y,
                                        .cameraHeading = coney::human::headingOf(forward) * kDegreesPerRadian};
            const std::array<int, 2> stick = coney::steerStick(view, aim.x, aim.y, kWalkPercent);
            frame(stick[0], stick[1]);
        }
        return false;
    }

    // Walks player 1 to `station`'s prompt and stops there: from the main room down the gym's stairs first (the gym
    // lies under the main room, and the planner, which looks in plan, would send him at the rail above it).
    bool goToStation(const Station& station, int limit) {
        if (player().position().z > kGymFloorAbove) {
            if (!walkTo(kStairsTop, kWaypointReach, limit) || !walkTo(kStairsFoot, kWaypointReach, limit)) {
                return false;
            }
        }
        const bool there = walkTo(station.prompt, kPromptReach, limit);
        idle(2);
        return there;
    }

    // The scripts' traced calls, and how many from `from` on contain `text`.
    const std::vector<std::string>& trace() const { return m_trace; }
    std::size_t traced(std::size_t from, std::string_view text) const { return countFrom(m_trace, from, text); }
    // The frames of play so far.
    std::uint64_t playFrames() const { return m_run.playFrames(); }
    coney::script::ScriptSystem& scripts() { return m_scripts; }

  private:
    // The route's waypoints from player 1 to `point` (the nodes' positions, then the point); straight when the level
    // has no path data or the line is walkable.
    std::vector<Vec3> routeTo(Vec3 point) {
        std::vector<Vec3> waypoints;
        const coney::ai::Brains* brains = m_run.session().gameplay().brains();
        coney::ai::RoutePlanner* planner = brains != nullptr ? brains->planner() : nullptr;
        if (planner == nullptr) {
            return waypoints;
        }
        auto plan = planner->request(player().position(), point);
        if (plan && plan->route) {
            for (const std::uint32_t node : plan->route->nodes()) {
                waypoints.push_back(planner->map().nodes()[node].position);
            }
        }
        return waypoints;
    }

    coney::test::LivePad m_pad;
    coney::test::DiscSession m_run;
    coney::script::ScriptSystem& m_scripts;
    std::vector<std::string> m_trace;
};

} // namespace

TEST_CASE("the disc's level95: triangle at the heavy bag walks the player to it and starts the bag's game",
          "[disc][story][hub][workout]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Gym gym(*wad);
    REQUIRE(gym.start());
    // The hub's opening holds the player a while; then he walks to the bag's prompt.
    const Station& bag = kStations[0];
    REQUIRE(gym.goToStation(bag, 3000));
    const std::size_t from = gym.trace().size();
    gym.frame(0, 0, "triangle");
    // worksetup.StartWorkout; the walk and turn to fWorkout[1] arrive (message 8: worksetup.DoWorkout) well inside
    // worksetup.TimeOut's 5 s; the bag's game starts with its 26 s clock.
    REQUIRE(gym.idleUntil([&] { return gym.traced(from, "> worksetup.DoWorkout(") == 1; }, 60));
    CHECK(gym.traced(from, "> worksetup.StartWorkout(") == 1);
    REQUIRE(gym.idleUntil([&] { return gym.traced(from, "W_SetStopWatch(26000, 0, \"workout.WorkoutTimeOut\")") > 0; },
                          60));
    CHECK(gym.traced(from, "> worksetup.TimeOut(") == 0);
    std::printf("  level95 heavy bag: started %zu, arrived %zu, clock set %zu, timed out %zu\n",
                gym.traced(from, "> worksetup.StartWorkout("), gym.traced(from, "> worksetup.DoWorkout("),
                gym.traced(from, "W_SetStopWatch(26000"), gym.traced(from, "> worksetup.TimeOut("));
}
