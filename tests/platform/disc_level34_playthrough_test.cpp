// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `level34` (mission 4, the riot) plays from each checkpoint to the next,
// and from checkpoint 5 to the mission's end, driven by the pad as a player drives it
// (docs/guides/testing.md#playthrough-tests). Each checkpoint starts in the game session as `coney --play-level level34
// --checkpoint N` starts it (support/disc_session.h), one test case per checkpoint. A driver reads where player 1
// stands each frame and walks him along the level's flag net (the `PedNet_*` flags, read from the level) with the left
// stick at 70 % turned by the camera, sprints with L2 where the mission is a race, climbs the fences in the way with
// triangle, steps round a prop that stops him and into the trigger box (read from the level) that ends the checkpoint.
// Checkpoint 3, the three objectives, is played by tests/platform/disc_level34_objectives_test.cpp, which goes on to
// SetCheckPoint(4).
//
// docs/research/scripting.md#level34 is the map: the boxes each checkpoint ends at, and in its fix list
// (#level34-fixes) the strip of `vSection3Gate` (item 7), the three objectives (item 10), the fence at (-131.5,
// -260.75) and the wait for the crew at the flag (item 11) and the sprint to `vSubwayExit` (item 15).
//
// They run only when the environment variable CONEY_DISC names the disc and skip otherwise; they print counts and
// positions only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brains.h"
#include "ai/route_planner.h"
#include "animation/anim_math.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/start_up_flow.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/each_case.h"
#include "warriors/created_humans.h"
#include "world_objects/flags.h"
#include "world_objects/volume_boxes.h"

namespace {

using coney::anim::Vec3;
using coney::human::Traversal;

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegreesPerRadian = 180.0F / kPi;

// A pad the driver sets before each frame: the left stick in per cent as an input script writes it (each axis laid
// out past its dead zone, stickByteFromPercent), the buttons held and the buttons pressed for one frame.
class DriverPad final : public coney::InputSource {
  public:
    // The left stick (x right, y up, per cent), the buttons held and the buttons tapped on the next frame only.
    int stickX = 0;
    int stickY = 0;
    std::uint16_t held = 0;
    std::uint16_t tap = 0;

    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        pad.buttons = static_cast<std::uint16_t>(held | tap);
        pad.sticks[2] = coney::stickByteFromPercent(stickX);
        pad.sticks[3] = coney::stickByteFromPercent(-stickY);
        tap = 0;
        return samples;
    }
};

// How a walk moves: the stick's deflection in per cent (a walk below the run's 0.95, a run at full deflection),
// whether L2 is held (the sprint) and whether triangle is tapped when a face stops him (a fence in the way is climbed).
struct Gait {
    int percent = 70;
    bool sprint = false;
    bool climb = false;
};
constexpr Gait kWalk{.percent = 70, .sprint = false, .climb = false};
constexpr Gait kClimb{.percent = 70, .sprint = false, .climb = true};
constexpr Gait kSprint{.percent = 100, .sprint = true, .climb = false};

// How near a waypoint counts as passed, m.
constexpr float kWaypointReach = 1.2F;
// Frames he moves less than kStillDistance (m) in a row before the driver looks for the way round.
constexpr int kStillFrames = 45;
constexpr float kStillDistance = 0.02F;
// Frames a climbing walk is held against a face before triangle is tapped.
constexpr int kClimbAfter = 6;
// How far aside the driver steps round a prop that stops him where the paths see a clear line, m.
constexpr float kSidestep = 2.5F;
// Frames between cross taps while the scripts hold player 1 (a scene plays: cross skips it, as a player skips one).
constexpr std::uint64_t kSkipEvery = 30;
// How long one waypoint and one wait may take, in frames.
constexpr int kLegLimit = 1800;
constexpr int kWaitLimit = 7200;

// The distance in plan between two points.
float planDistance(Vec3 a, Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// One run of level34 from a checkpoint, the scripts' calls traced, player 1 driven by a DriverPad.
class Riot {
  public:
    Riot(const coney::io::Wad& wad, int checkpoint) : m_run(wad, m_pad), m_scripts(m_run.session().flow().scripts()) {
        m_run.session().startAtLevel("level34", checkpoint, [this] {
            // Each line as the trace gives it, without its line break.
            m_scripts.traceCalls([this](std::string_view line) {
                while (line.ends_with('\n') || line.ends_with('\r')) {
                    line.remove_suffix(1);
                }
                m_trace.emplace_back(line);
            });
        });
    }

    // Runs until the level plays; whether it does.
    bool start() { return m_run.runUntilPlay(); }

    // The play mode (null before the level plays and after the mission ends) and player 1's human.
    coney::platform::PlayLevelMode* play() { return m_run.session().play(); }
    const coney::human::Human& player() { return play()->player().human(); }
    // Player 1's feet.
    Vec3 feet() { return player().position(); }
    // Whether the pad drives player 1 now (a scene or the scripts hold him otherwise).
    bool padControlled() { return play() != nullptr && play()->player().padControlled(); }

    // One frame with the pad as set; while the scripts hold player 1, cross is tapped now and then to skip a scene.
    void frame() {
        if (play() != nullptr && !padControlled() && m_run.playFrames() % kSkipEvery == 0) {
            m_pad.tap |= coney::pad::kCross;
        }
        m_run.run(1);
        noteDeath();
    }
    // Idles until `done` holds, at most `limit` frames; whether it did.
    bool idleUntil(const std::function<bool()>& done, int limit) {
        rest();
        for (int i = 0; i < limit && !done(); ++i) {
            frame();
        }
        return done();
    }

    // The flag named `name`'s position; fails the case when the level has none.
    Vec3 flag(std::string_view name) {
        const coney::world_objects::WorldFlags& flags = m_run.session().flow().flags();
        const std::optional<double> handle = flags.findByName(name);
        const coney::world_objects::WorldFlag* found = handle ? flags.find(*handle) : nullptr;
        INFO("flag " << name);
        REQUIRE(found != nullptr);
        return Vec3{found->position[0], found->position[1], found->position[2]};
    }
    // The centre of the volume box named `name` (a turned box turns about its centre); fails the case without one.
    Vec3 box(std::string_view name) {
        const coney::world_objects::VolumeBox* found = nullptr;
        for (const coney::world_objects::VolumeBox& candidate : m_run.session().flow().boxes().all()) {
            found = candidate.name == name ? &candidate : found;
        }
        INFO("box " << name);
        REQUIRE(found != nullptr);
        return Vec3{(found->low[0] + found->high[0]) / 2.0F, (found->low[1] + found->high[1]) / 2.0F,
                    (found->low[2] + found->high[2]) / 2.0F};
    }

    // Walks player 1 to within `reach` of `point` at `gait`: steers at it through the camera as it faces now, waits
    // while the scripts hold him and stops early when `stop` holds. Whether he got there (or `stop` held) within
    // `limit` frames.
    bool walkTo(Vec3 point, float reach, Gait gait, int limit, const std::function<bool()>& stop = {}) {
        Vec3 last = feet();
        int still = 0;
        std::vector<Vec3> detour;
        std::size_t next = 0;
        for (int i = 0; i < limit; ++i) {
            if ((stop && stop()) || planDistance(feet(), point) <= reach) {
                rest();
                return true;
            }
            if (!padControlled()) {
                rest();
                frame();
                continue;
            }
            still = planDistance(feet(), last) < kStillDistance ? still + 1 : 0;
            last = feet();
            const Traversal traversal = player().traversal();
            m_climbs += traversal == Traversal::Climbing && !m_climbing ? 1 : 0;
            m_climbing = traversal == Traversal::Climbing;
            // A face in the way: a fence the paths cross with a climb (kind 8), taken with triangle.
            if (gait.climb && still >= kClimbAfter && traversal == Traversal::None && i % 2 == 0) {
                m_pad.tap |= coney::pad::kTriangle;
            }
            if (still >= kStillFrames) {
                // Blocked: the way round on the level's paths, or where the paths see a clear line (a prop or a crate
                // in the way), a step aside, one side and then the other on the next stop.
                ++m_stuck;
                still = 0;
                detour = routeTo(point);
                next = 0;
                if (detour.empty()) {
                    detour = {sidestep(point), point};
                }
            }
            while (next < detour.size() && planDistance(feet(), detour[next]) <= kWaypointReach) {
                ++next;
            }
            steer(next < detour.size() ? detour[next] : point, gait);
            frame();
        }
        rest();
        return stop && stop();
    }
    // Walks player 1 through `route` at `gait`, each waypoint within kWaypointReach, the last within `reach`, and stops
    // early when `stop` holds. Whether he got through within `limit` frames per waypoint.
    bool walkRoute(std::span<const Vec3> route, float reach, Gait gait, int limit,
                   const std::function<bool()>& stop = {}) {
        for (std::size_t i = 0; i < route.size(); ++i) {
            const bool last = i + 1 == route.size();
            if (!walkTo(route[i], last ? reach : kWaypointReach, gait, limit, stop)) {
                return false;
            }
            if (stop && stop()) {
                return true;
            }
        }
        return true;
    }

    // How many trace lines start with `text`.
    [[nodiscard]] std::size_t count(std::string_view text) const {
        return static_cast<std::size_t>(
            std::ranges::count_if(m_trace, [text](const std::string& line) { return line.starts_with(text); }));
    }
    // The index of the first trace line from line `from` on that starts with `text`, if any.
    [[nodiscard]] std::optional<std::size_t> first(std::string_view text, std::size_t from = 0) const {
        for (std::size_t i = from; i < m_trace.size(); ++i) {
            if (m_trace[i].starts_with(text)) {
                return i;
            }
        }
        return std::nullopt;
    }
    // The index of the first engine call of the script function `function` for player 1 (the handler's last argument
    // his handle: a box's message 3 comes for every human who enters), if any.
    [[nodiscard]] std::optional<std::size_t> firstByPlayer(std::string_view function) {
        const coney::HumanCreation* creation = m_run.session().flow().humans().player(1);
        REQUIRE(creation != nullptr);
        const std::string start = std::format("> {}(", function);
        const std::string end = std::format(", {})", creation->handle);
        const auto found = std::ranges::find_if(
            m_trace, [&](const std::string& line) { return line.starts_with(start) && line.ends_with(end); });
        return found != m_trace.end() ? std::optional<std::size_t>(found - m_trace.begin()) : std::nullopt;
    }
    // Whether the scripts called SetCheckPoint(`checkpoint`).
    [[nodiscard]] bool reached(int checkpoint) const { return count(std::format("SetCheckPoint({})", checkpoint)) > 0; }
    // Whether the session logged a failed mission.
    [[nodiscard]] bool failed() const {
        return std::ranges::any_of(m_run.log(),
                                   [](const std::string& line) { return line.starts_with("mission failed"); });
    }

    // Prints one line of what the run did, prefixed by `what`.
    void report(std::string_view what) {
        const Vec3 at = play() != nullptr ? feet() : Vec3{};
        std::printf("  level34 %.*s: %llu frames of play, at (%.1f, %.1f, %.1f), climbs %d, stops %d, deaths %d, "
                    "%llu script errors\n",
                    static_cast<int>(what.size()), what.data(), static_cast<unsigned long long>(m_run.playFrames()),
                    at.x, at.y, at.z, m_climbs, m_stuck, m_deaths, static_cast<unsigned long long>(m_scripts.errors()));
    }

    [[nodiscard]] int deaths() const { return m_deaths; }
    [[nodiscard]] int climbs() const { return m_climbs; }
    [[nodiscard]] coney::script::ScriptSystem& scripts() { return m_scripts; }
    [[nodiscard]] coney::test::DiscSession& run() { return m_run; }

  private:
    // The level's path nodes from player 1 to `point`, then the point; empty when the level has no path data or the
    // paths see a clear straight line.
    std::vector<Vec3> routeTo(Vec3 point) {
        std::vector<Vec3> waypoints;
        const coney::ai::Brains* brains = m_run.session().gameplay().brains();
        coney::ai::RoutePlanner* planner = brains != nullptr ? brains->planner() : nullptr;
        if (planner == nullptr) {
            return waypoints;
        }
        auto plan = planner->request(feet(), point);
        // Moved into a local optional so the check and the use are one variable (clang-tidy cannot follow the plan's).
        const std::optional<coney::ai::Route> route = plan ? std::move(plan->route) : std::nullopt;
        if (route) {
            for (const std::uint32_t node : route->nodes()) {
                waypoints.push_back(planner->map().nodes()[node].position);
            }
            waypoints.push_back(point);
        }
        return waypoints;
    }
    // A point kSidestep to one side of player 1, across his line to `point`: left on one stop, right on the next.
    Vec3 sidestep(Vec3 point) {
        const Vec3 at = feet();
        const float side = m_stuck % 2 == 0 ? 1.0F : -1.0F;
        const float across = kSidestep * side / std::max(planDistance(at, point), 1e-3F);
        return Vec3{at.x - ((point.y - at.y) * across), at.y + ((point.x - at.x) * across), at.z};
    }
    // The stick at rest and nothing held.
    void rest() {
        m_pad.stickX = 0;
        m_pad.stickY = 0;
        m_pad.held = 0;
    }
    // Sets the stick toward `point` at `gait`, turned by the camera as it faces now (up is the camera's view along the
    // ground, docs/research/characters-locomotion.md#input), and L2 when the gait sprints.
    void steer(Vec3 point, Gait gait) {
        const Vec3 forward = player().record().cameraForward;
        const coney::SteerView view{
            .x = feet().x, .y = feet().y, .cameraHeading = coney::human::headingOf(forward) * kDegreesPerRadian};
        const std::array<int, 2> stick = coney::steerStick(view, point.x, point.y, gait.percent);
        m_pad.stickX = stick[0];
        m_pad.stickY = stick[1];
        m_pad.held = gait.sprint ? coney::pad::kL2 : 0;
    }
    // Counts player 1's deaths: a frame on which his health has run out after one on which it had not.
    void noteDeath() {
        const bool dead = play() != nullptr && player().health().depleted();
        m_deaths += dead && !m_wasDead ? 1 : 0;
        m_wasDead = dead;
    }

    DriverPad m_pad;
    coney::test::DiscSession m_run;
    coney::script::ScriptSystem& m_scripts;
    std::vector<std::string> m_trace;
    int m_climbs = 0;
    int m_stuck = 0;
    int m_deaths = 0;
    bool m_wasDead = false;
    bool m_climbing = false;
};

// Checkpoint 1: out of the subway past vStartIntroLooters, up the stairs into vEnterRiot, over the fence into vIntro
// (the car flippers' scene, then the loot objective), then along the flag net east and over the street's fence at
// x -42, south down the far street and over its fence at y -116.7 (both kind-8 climb links of the level's paths), and
// west along y -143 into vSection2, which calls SetCheckPoint(2) (scripting.md#level34 for the boxes; the route is
// inferred from the path data and Coney's run and is not yet on the page: #1575).
void playStreet(Riot& riot) {
    const auto done = [&riot] { return riot.reached(2); };
    riot.walkTo(riot.box("vStartIntroLooters"), 1.0F, kWalk, kLegLimit, done);
    riot.walkTo(riot.box("vEnterRiot"), 0.8F, kWalk, kLegLimit, done);
    riot.walkTo(riot.box("vIntro"), 1.0F, kClimb, kLegLimit, done);
    riot.idleUntil([&riot] { return riot.padControlled(); }, kWaitLimit);
    const std::array route{riot.flag("PedNet_06"),      riot.flag("PedNet_07"),      Vec3{-49.9F, -110.4F, 0.0F},
                           Vec3{-35.6F, -109.6F, 0.0F}, riot.flag("PedNet_10"),      riot.flag("PedNet_11"),
                           Vec3{-42.6F, -144.1F, 0.0F}, Vec3{-53.8F, -139.9F, 0.0F}, Vec3{-58.0F, -140.0F, 0.0F}};
    riot.walkRoute(route, 0.8F, kClimb, kLegLimit, done);
    riot.idleUntil(done, 300);
}

// Checkpoint 2: to vCarPoizo (the stereo scene, after which P1.DoneStereoPoizo sets the radio objective and
// vSection3Gate), then along the flag net to the gate and south into the 1.8 m strip of vSection3Gate, which calls
// SetCheckPoint(3) (items 6 and 7).
void playStores(Riot& riot) {
    const auto done = [&riot] { return riot.reached(3); };
    const auto stereo = [&riot] { return riot.count("> P1.DoneStereoPoizo(") > 0; };
    riot.walkTo(riot.box("vCarPoizo"), 0.5F, kWalk, kLegLimit, stereo);
    riot.idleUntil([&riot, &stereo] { return stereo() && riot.padControlled(); }, kWaitLimit);
    const std::array route{riot.flag("PedNet_12"),       riot.flag("PedNet_13"),      riot.flag("PedNet_14"),
                           riot.flag("PedNet_15"),       riot.flag("PedNet_16"),      riot.flag("PedNet_22"),
                           Vec3{-107.5F, -221.0F, 0.0F}, Vec3{-107.5F, -223.1F, 0.0F}};
    riot.walkRoute(route, 0.3F, kWalk, kLegLimit, done);
    riot.idleUntil(done, 300);
}

// Checkpoint 4: west along the flag net through DblGt00 into the dead-end alley, over its chain-link fence at
// (-131.5, -260.75) with triangle, to fFinalGate and a wait there until the crew is near and P2.AtGate opens the
// second gate, then through it to vNearFuries, which calls SetCheckPoint(5) (items 11 and 12).
void playGate(Riot& riot) {
    const auto done = [&riot] { return riot.reached(5); };
    const std::array route{riot.flag("PedNet_30"), riot.flag("PedNet_33"),       riot.flag("PedNet_25"),
                           riot.flag("PedNet_34"), Vec3{-125.0F, -256.0F, 0.0F}, Vec3{-131.5F, -258.5F, 0.0F}};
    riot.walkRoute(route, 0.5F, kWalk, kLegLimit, done);
    riot.walkTo(Vec3{-131.5F, -262.5F, 0.0F}, 0.5F, kClimb, kLegLimit, done);
    const std::size_t opened = riot.count("DoorOpen(");
    riot.walkTo(riot.flag("fFinalGate"), 1.0F, kWalk, kLegLimit, done);
    riot.idleUntil([&riot, opened] { return riot.count("DoorOpen(") > opened; }, kWaitLimit);
    riot.walkTo(riot.box("vNearFuries"), 0.8F, kSprint, kLegLimit, done);
    riot.idleUntil(done, 300);
}

// Checkpoint 5: the park scene, then the sprint (L2 held, full stick) past the Furies to vSubwayExit by the route of
// item 15, the scene l34_c2 and, at its end, HUDLaunchMissionComplete.
void playPark(Riot& riot) {
    const auto exitReached = [&riot] { return riot.count("> P3.GetToSubway(") > 0; };
    riot.idleUntil([&riot] { return riot.count("ScheduleFunc(\"P3.FuryTimeout\"") > 0 && riot.padControlled(); },
                   kWaitLimit);
    const std::array route{Vec3{-230.0F, -270.0F, 0.0F}, Vec3{-245.1F, -249.0F, 0.0F}, Vec3{-252.4F, -222.0F, 0.0F},
                           Vec3{-248.5F, -213.5F, 0.0F}, Vec3{-254.5F, -202.2F, 0.0F}, riot.box("vSubwayExit")};
    riot.walkRoute(route, 0.3F, kSprint, kLegLimit, exitReached);
    riot.idleUntil([&riot] { return riot.count("HUDLaunchMissionComplete(") > 0; }, kWaitLimit);
}

// One checkpoint's case: its number, what it plays and the driver that plays it.
struct Checkpoint {
    int number = 0;
    std::string_view what;
    void (*play)(Riot&) = nullptr;
};
constexpr std::array<Checkpoint, 4> kCheckpoints{{
    {1, "from the subway over the fences into section 2", playStreet},
    {2, "through the stereo scene into the strip of vSection3Gate", playStores},
    {4, "over the alley's fence to the flag and on to the Furies", playGate},
    {5, "through the park to the subway exit and the mission's end", playPark},
}};

// Plays `checkpoint` from its start and checks that it reaches the next, or at 5 the mission's end, with no script
// error, no failed mission and no death.
//
// Expected: scripting.md#level34, inferred (the boxes and calls that end each checkpoint)
// Expected: scripting.md#level34-fixes, confirmed (runtime) (the routes of items 7, 11 and 15)
void playCheckpoint(const Checkpoint& checkpoint) {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Riot riot(*wad, checkpoint.number);
    REQUIRE(riot.start());
    checkpoint.play(riot);
    riot.report(std::format("checkpoint {}", checkpoint.number));
    CHECK(riot.scripts().errors() == 0);
    CHECK_FALSE(riot.failed());
    CHECK(riot.deaths() == 0);
    const auto before = [](std::optional<std::size_t> a, std::optional<std::size_t> b) { return a && b && *a < *b; };
    const std::optional<std::size_t> next = riot.first(std::format("SetCheckPoint({})", checkpoint.number + 1));
    switch (checkpoint.number) {
    case 1:
        // The player's own entries: vEnterRiot, then vIntro (the car flippers' scene), then vSection2.
        CHECK(before(riot.firstByPlayer("P1.EnterRiot"), riot.firstByPlayer("P1.Intro")));
        CHECK(before(riot.firstByPlayer("P1.Intro"), riot.firstByPlayer("P1.EnterSection2")));
        CHECK(before(riot.firstByPlayer("P1.EnterSection2"), next));
        // Expected: unverified, #1575 (the vIntro fence and the two street fences of the route above)
        CHECK(riot.climbs() >= 3);
        break;
    case 2:
        CHECK(before(riot.first("> P1.DoneStereoPoizo("), riot.firstByPlayer("P1.EnterSection3")));
        CHECK(before(riot.firstByPlayer("P1.EnterSection3"), next));
        break;
    case 4:
        // P2.SetupGate opens DblGt00 at the start and P2.AtGate DblGt02 once the crew is at the flag.
        CHECK(riot.count("DoorOpen(") == 2);
        CHECK(riot.climbs() >= 1);
        CHECK(before(riot.firstByPlayer("P2.ReachPark"), next));
        break;
    default:
        break;
    }
    if (checkpoint.number < 5) {
        CHECK(riot.count(std::format("SetCheckPoint({})", checkpoint.number + 1)) == 1);
    } else {
        // The subway exit, then the outro scene l34_c2, whose end runs PreCashTheWorld and so
        // HUDLaunchMissionComplete.
        const std::optional<std::size_t> exit = riot.first("> P3.GetToSubway(");
        const std::optional<std::size_t> complete = riot.first("HUDLaunchMissionComplete(");
        const std::optional<std::size_t> sceneEnd = riot.first("> PreCashTheWorld(", exit.value_or(0));
        CHECK(exit.has_value());
        CHECK(before(exit, sceneEnd));
        CHECK(before(sceneEnd, complete));
        CHECK(riot.count("HUDLaunchMissionComplete(") == 1);
    }
}

[[maybe_unused]] const bool kCheckpointsRegistered = coney::test::registerEach(
    kCheckpoints, "[disc][story][playthrough]",
    [](const Checkpoint& checkpoint) {
        return std::format("the disc's level34 checkpoint {}, driven by the pad: {}", checkpoint.number,
                           checkpoint.what);
    },
    [](const Checkpoint& checkpoint) { playCheckpoint(checkpoint); });

} // namespace
