// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A disc test's player 1 driven with the left stick as a player walks him (docs/guides/testing.md#playthrough-tests):
// one frame at a time through a LivePad, along the level's path data at a partial deflection, planning again when he
// stops moving. Shared by the hub's playthroughs (the gym's stations, the clubhouse's trophies). No game data.

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/brains.h"
#include "ai/route_planner.h"
#include "core/input_script.h"
#include "gamemodes/gameplay_mode.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_session.h"
#include "support/live_pad.h"

namespace coney::test {

/// Player 1 of a DiscSession steered frame by frame: the pad at rest, taps, and walks along the level's paths.
class StickWalker {
  public:
    /// How far the stick is pushed while walking (a walk, below the run's 0.95).
    static constexpr int kWalkPercent = 60;
    /// How near the walker goes to a waypoint before taking the next.
    static constexpr float kWaypointReach = 0.6F;
    /// Frames he moves less than kStillDistance (m) in a row, after which the walker plans its route again.
    static constexpr int kStillFrames = 30;
    static constexpr float kStillDistance = 0.02F;

    /// Steers player 1 of `run` through `pad`, the pad `run` was made with; both must outlive the walker.
    StickWalker(DiscSession& run, LivePad& pad) : m_run(run), m_pad(pad) {}

    /// The play mode; null before a level plays.
    platform::PlayLevelMode* play() { return m_run.session().play(); }
    /// Player 1's human (a level must be playing).
    const human::Human& player() { return play()->player().human(); }
    /// Whether the pad drives player 1 now (the scripts hold him otherwise).
    bool padControlled() { return play()->player().padControlled(); }

    /// One frame with the left stick at (x, y) per cent and `taps` (input script button names, space-separated)
    /// pressed for this frame.
    void frame(int x = 0, int y = 0, std::string_view taps = {}) {
        const std::uint64_t at = m_pad.nextFrame();
        std::string script = std::format("{} stick left {} {}\n", at, x, y);
        if (!taps.empty()) {
            script += std::format("{} tap {}\n", at, taps);
        }
        m_pad.play(script);
        m_run.run(1);
    }
    /// `frames` frames with the pad at rest.
    void idle(int frames) {
        for (int i = 0; i < frames; ++i) {
            frame();
        }
    }
    /// Idles until `done` holds, at most `limit` frames; whether it did.
    bool idleUntil(const std::function<bool()>& done, int limit) {
        for (int i = 0; i < limit && !done(); ++i) {
            frame();
        }
        return done();
    }

    /// Walks player 1 to within `reach` of `point` (in plan) with the stick at kWalkPercent, along the level's paths:
    /// waits while the scripts hold him, plans a route from where he stands (again whenever he has not moved for
    /// kStillFrames) and steers at each waypoint through the camera as it faces now. Whether he got there within
    /// `limit` frames.
    bool walkTo(anim::Vec3 point, float reach, int limit) {
        std::vector<anim::Vec3> waypoints;
        bool planned = false;
        std::size_t next = 0;
        anim::Vec3 lastPlace = player().position();
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
            const anim::Vec3 aim = next < waypoints.size() ? waypoints[next] : point;
            const anim::Vec3 forward = player().record().cameraForward;
            const SteerView view{.x = player().position().x,
                                 .y = player().position().y,
                                 .cameraHeading = human::headingOf(forward) * kDegreesPerRadian};
            const std::array<int, 2> stick = steerStick(view, aim.x, aim.y, kWalkPercent);
            frame(stick[0], stick[1]);
        }
        return false;
    }

    /// The distance in plan between two points.
    static float planDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

  private:
    static constexpr float kDegreesPerRadian = 57.29578F;

    // The route's waypoints from player 1 to `point` (the nodes' positions, then the point); straight when the level
    // has no path data or the line is walkable.
    std::vector<anim::Vec3> routeTo(anim::Vec3 point) {
        std::vector<anim::Vec3> waypoints;
        const ai::Brains* brains = m_run.session().gameplay().brains();
        ai::RoutePlanner* planner = brains != nullptr ? brains->planner() : nullptr;
        if (planner == nullptr) {
            return waypoints;
        }
        auto plan = planner->request(player().position(), point);
        // Moved into a local optional so the check and the use are one variable (clang-tidy cannot follow the plan's).
        const std::optional<ai::Route> route = plan ? std::move(plan->route) : std::nullopt;
        if (route) {
            for (const std::uint32_t node : route->nodes()) {
                waypoints.push_back(planner->map().nodes()[node].position);
            }
        }
        return waypoints;
    }

    DiscSession& m_run;
    LivePad& m_pad;
};

} // namespace coney::test
