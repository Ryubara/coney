// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/route_planner.h"
#include "ai/threats.h"
#include "core/input_script.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"

// The hideout's crew are no threat to the player: walking up to them and past them never takes one as the target or
// locks on (issue #82, item 2). In the original's level95 their gangs (kind 0x13) are neither friends nor enemies of
// the player's, and the stance's enemy filter takes only threats (`TargetFilter_StanceEnemy` 0x002791b0): walking past
// Cochise held 1.2 m beside the player for 40 updates left his target none and his state word 0 (PCSX2 2.9.129,
// docs/research/combat-targets.md#fight-stance). The run starts at checkpoint 2 through the game session, as the story
// does, and walks with the stick at 60 % along the level's paths. It prints counts only.

namespace {

using coney::anim::Vec3;

constexpr int kWalkPercent = 60;
constexpr float kWaypointReach = 0.6F;
constexpr float kDegreesPerRadian = 57.29578F;
// How near the driver walks to each crew mate, and how many of them it visits.
constexpr float kBeside = 1.0F;
constexpr std::size_t kVisits = 3;
// Frames each walk up to one may take, and the frames it walks on past him.
constexpr int kWalkFrames = 900;
constexpr int kPastFrames = 45;

// The distance in plan between two points.
float planDistance(Vec3 a, Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// level95 at checkpoint 2 with a pad the driver steers frame by frame.
class Hideout {
  public:
    explicit Hideout(const coney::io::Wad& wad) : m_run(wad, m_pad) { m_run.session().startAtLevel("level95", 2, {}); }

    // Runs until the level plays; whether it does.
    bool start() { return m_run.runUntilPlay(); }

    // The play mode and player 1's human.
    coney::platform::PlayLevelMode* play() { return m_run.session().play(); }
    const coney::human::Human& player() { return play()->player().human(); }
    bool padControlled() { return play()->player().padControlled(); }

    // The crew mates: AI humans the player may fight (no friends of his gang) that are no threat to him.
    std::vector<const coney::human::Human*> crew() {
        std::vector<const coney::human::Human*> found;
        coney::ai::Brains& brains = play()->fighters().brains();
        const coney::ai::Brain* mine = brains.find(player());
        if (mine == nullptr) {
            return found;
        }
        for (std::size_t i = 0; i < brains.size(); ++i) {
            const coney::ai::Brain& brain = brains.at(i);
            if (&brain == mine || brain.gang() == nullptr || coney::ai::Gangs::friends(mine->gang(), brain.gang()) ||
                coney::ai::isThreat(*mine, brain) || brain.human().health().depleted()) {
                continue;
            }
            found.push_back(&brain.human());
        }
        return found;
    }

    // One frame with the left stick at (x, y) per cent; after it, what the player's fighter holds.
    void frame(int x = 0, int y = 0) {
        m_pad.play(std::format("{} stick left {} {}\n", m_pad.nextFrame(), x, y));
        m_run.run(1);
        const coney::human::Fighter& fighter = player().fighter();
        ++m_frames;
        m_targeted += fighter.target() != nullptr ? 1 : 0;
        m_locked += fighter.lockTarget() != nullptr ? 1 : 0;
        m_stance += fighter.inFightStance() ? 1 : 0;
    }

    // Walks player 1 toward `aim()` (re-read every frame, the crew walk about) along the level's paths until he is
    // within `reach` of it; whether he got there within `limit` frames.
    template <typename Aim> bool walkTo(Aim aim, float reach, int limit) {
        std::vector<Vec3> waypoints;
        std::size_t next = 0;
        for (int i = 0; i < limit; ++i) {
            const Vec3 point = aim();
            if (planDistance(player().position(), point) <= reach) {
                return true;
            }
            if (!padControlled()) {
                frame();
                continue;
            }
            if (i % 30 == 0) {
                waypoints = routeTo(point);
                next = 0;
            }
            while (next < waypoints.size() && planDistance(player().position(), waypoints[next]) <= kWaypointReach) {
                ++next;
            }
            steerAt(next < waypoints.size() ? waypoints[next] : point);
        }
        return false;
    }

    // A frame with the stick pushed toward `point` through the camera as it faces now.
    void steerAt(Vec3 point) {
        const Vec3 forward = player().record().cameraForward;
        const coney::SteerView view{.x = player().position().x,
                                    .y = player().position().y,
                                    .cameraHeading = coney::human::headingOf(forward) * kDegreesPerRadian};
        const std::array<int, 2> stick = coney::steerStick(view, point.x, point.y, kWalkPercent);
        frame(stick[0], stick[1]);
    }

    std::uint64_t frames() const { return m_frames; }
    std::uint64_t targeted() const { return m_targeted; }
    std::uint64_t locked() const { return m_locked; }
    std::uint64_t stance() const { return m_stance; }

  private:
    // The level's route from the player to `point`, as waypoints; empty without a planner or a route.
    std::vector<Vec3> routeTo(Vec3 point) {
        std::vector<Vec3> waypoints;
        const coney::ai::Brains* brains = m_run.session().gameplay().brains();
        coney::ai::RoutePlanner* planner = brains != nullptr ? brains->planner() : nullptr;
        if (planner == nullptr) {
            return waypoints;
        }
        auto plan = planner->request(player().position(), point);
        const std::optional<coney::ai::Route> route = plan ? std::move(plan->route) : std::nullopt;
        if (route) {
            for (const std::uint32_t node : route->nodes()) {
                waypoints.push_back(planner->map().nodes()[node].position);
            }
        }
        return waypoints;
    }

    coney::test::LivePad m_pad;
    coney::test::DiscSession m_run;
    std::uint64_t m_frames = 0;
    std::uint64_t m_targeted = 0;
    std::uint64_t m_locked = 0;
    std::uint64_t m_stance = 0;
};

} // namespace

TEST_CASE("the disc's level95: walking up to the crew and past them never takes one as the target or locks on",
          "[disc][story][hub][combat]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Hideout hideout(*wad);
    REQUIRE(hideout.start());
    std::vector<const coney::human::Human*> crew = hideout.crew();
    REQUIRE(crew.size() >= kVisits);
    // The nearest first.
    const Vec3 from = hideout.player().position();
    std::ranges::sort(crew, {}, [&](const coney::human::Human* h) { return planDistance(from, h->position()); });
    std::size_t reached = 0;
    std::uint64_t near = 0;
    for (std::size_t visit = 0; visit < kVisits; ++visit) {
        const coney::human::Human& mate = *crew[visit];
        if (!hideout.walkTo([&] { return mate.position(); }, kBeside, kWalkFrames)) {
            continue;
        }
        ++reached;
        // On past him: the stick toward a point 3 m beyond him on the line he was reached along.
        const Vec3 at = hideout.player().position();
        const Vec3 dir{mate.position().x - at.x, mate.position().y - at.y, 0.0F};
        const float length = std::max(std::hypot(dir.x, dir.y), 1e-3F);
        const Vec3 beyond{mate.position().x + (3.0F * dir.x / length), mate.position().y + (3.0F * dir.y / length),
                          at.z};
        for (int i = 0; i < kPastFrames; ++i) {
            near += planDistance(hideout.player().position(), mate.position()) <= 2.0F ? 1 : 0;
            hideout.steerAt(beyond);
        }
    }
    std::printf("  level95 crew: %zu of %zu crew mates reached, %llu frames within 2 m of one, %llu frames: %llu with "
                "a target, %llu locked, %llu in the stance\n",
                reached, kVisits, static_cast<unsigned long long>(near),
                static_cast<unsigned long long>(hideout.frames()), static_cast<unsigned long long>(hideout.targeted()),
                static_cast<unsigned long long>(hideout.locked()), static_cast<unsigned long long>(hideout.stance()));
    // Expected: combat-targets.md#fight-stance, confirmed (runtime), traces target_walk_past_friends: a crew mate is
    // refused by the stance filter, so no target, no lock and no stance.
    REQUIRE(reached >= 2);
    CHECK(near > 20);
    CHECK(hideout.targeted() == 0);
    CHECK(hideout.locked() == 0);
    CHECK(hideout.stance() == 0);
}
