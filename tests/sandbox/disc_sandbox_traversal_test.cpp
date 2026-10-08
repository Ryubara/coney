// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: Rembrandt (loaded from the disc) on the parkour course
// (assets/sandbox/parkour.layout): the sprint and its stamina along the run-up lane, run and sprint jumps, a jump over
// a gap between platforms, climbs over fences and onto walls of each height band, and the walking body's step rule at a
// kerb and at ledges. Driven by the scripted pads in tests/support (partial and full stick deflections, L2 and
// triangle), on the fixed 30 Hz step with no window and no clock. The course is Coney's own; only the character comes
// from the disc. They run only when the environment variable CONEY_DISC names the disc and skip otherwise; they print
// positions, speeds and a hash of the run only, never data (LEGAL.md). CONEY_TRACE=1 prints every frame.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/character_data.h"
#include "core/input_script.h"
#include "core/pads.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "human/body.h"
#include "human/player.h"
#include "platform/render_engine.h"
#include "sandbox/sandbox_world.h"

using Catch::Approx;
using coney::human::Traversal;

namespace {

// What one frame of a scripted run left.
struct FrameRecord {
    coney::anim::Vec3 position;
    float speed = 0.0F;
    float verticalSpeed = 0.0F;
    int stamina = 0;
    std::uint32_t animId = 0;
    Traversal traversal = Traversal::None;
    bool airborne = false;
};

// One scripted run: its frames, a hash of their positions and how often the player was put back at the start.
struct Run {
    std::vector<FrameRecord> frames;
    std::uint64_t hash = 0;
    std::uint32_t respawns = 0;
};

// The player's character from the disc CONEY_DISC names, and the parkour course.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct Course {
    std::optional<coney::io::Wad> wad;
    std::unique_ptr<coney::human::PlayerCharacter> character;
    std::unique_ptr<coney::sandbox::SandboxWorld> world;
};

// Loads Rembrandt from the disc at `discPath` and builds the parkour layout from the repository's assets.
Course loadCourse(const char* discPath) {
    Course course;
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    const coney::io::Wad& discWad = course.wad.emplace(std::move(*wad));
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::characters::addCharacterDataHandlers(table);
    auto character = coney::human::PlayerCharacter::load(discWad, table, coney::human::kPlayerModel);
    REQUIRE(character.has_value());
    course.character = std::move(*character);
    auto world = coney::sandbox::SandboxWorld::load(std::filesystem::path(CONEY_ASSETS_DIR) / "sandbox", "parkour");
    REQUIRE(world.has_value());
    REQUIRE(world->collision() != nullptr);
    course.world = std::make_unique<coney::sandbox::SandboxWorld>(std::move(*world));
    return course;
}

// FNV-1a over `bytes`, continuing from `hash`.
std::uint64_t fnv1a(std::uint64_t hash, const void* bytes, std::size_t size) {
    const auto* data = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * 0x100000001b3ULL;
    }
    return hash;
}

// Runs `frames` frames of the input script `name` (in tests/support) on a fresh player standing at `start` (game axes)
// facing +y, and prints where he was every second (every frame with CONEY_TRACE set).
Run runScript(const Course& course, const char* label, const std::string& name, coney::anim::Vec3 start,
              std::uint64_t frames) {
    const std::filesystem::path path = std::filesystem::path(CONEY_TEST_SUPPORT_DIR) / name;
    auto events = coney::loadInputScript(path.string());
    REQUIRE(events.has_value());
    coney::ScriptedInput input(std::move(*events));
    coney::Pads pads;
    const coney::raycast::CollisionMesh* mesh = course.world->collision();
    coney::human::Player player(*course.character, mesh,
                                coney::human::PlayerStart{.position = start, .headingDegrees = 0.0F});
    const char* trace = SDL_getenv("CONEY_TRACE");
    const bool everyFrame = trace != nullptr && *trace != '\0';
    Run run;
    run.hash = 0xcbf29ce484222325ULL;
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        pads.update(input.sample(frame));
        player.update(pads.port(0), mesh);
        const coney::human::Human& human = player.human();
        run.frames.push_back(FrameRecord{.position = human.position(),
                                         .speed = human.speed(),
                                         .verticalSpeed = human.velocity().z,
                                         .stamina = human.stamina().value(),
                                         .animId = human.animator().animId(),
                                         .traversal = human.traversal(),
                                         .airborne = human.airborne()});
        // The hash takes positions to the millimetre, so it names the run on every compiler.
        for (const float axis : {human.position().x, human.position().y, human.position().z}) {
            const auto millimetres = static_cast<std::int32_t>(std::lround(axis * 1000.0F));
            run.hash = fnv1a(run.hash, &millimetres, sizeof millimetres);
        }
        if (everyFrame || (frame + 1) % 30 == 0) {
            const coney::anim::Vec3 p = human.position();
            std::printf("parkour %s: frame %3llu at (%.2f, %.2f, %.2f) speed %.2f stamina %d clip %u %s\n", label,
                        static_cast<unsigned long long>(frame) + 1, p.x, p.y, p.z, human.speed(),
                        human.stamina().value(), human.animator().animId(),
                        coney::human::traversalName(human.traversal()));
        }
    }
    run.respawns = player.respawns();
    std::printf("parkour %s: %llu frames, hash %016llx, %u respawns\n", label, static_cast<unsigned long long>(frames),
                static_cast<unsigned long long>(run.hash), run.respawns);
    return run;
}

// The highest the feet were during `run`.
float highest(const Run& run) {
    float top = run.frames.front().position.z;
    for (const FrameRecord& record : run.frames) {
        top = std::max(top, record.position.z);
    }
    return top;
}

// The lowest the feet were during `run`.
float lowest(const Run& run) {
    float bottom = run.frames.front().position.z;
    for (const FrameRecord& record : run.frames) {
        bottom = std::min(bottom, record.position.z);
    }
    return bottom;
}

// The fastest horizontal speed during `run`.
float fastest(const Run& run) {
    float top = 0.0F;
    for (const FrameRecord& record : run.frames) {
        top = std::max(top, record.speed);
    }
    return top;
}

// How many frames of `run` had `traversal`.
int framesOf(const Run& run, Traversal traversal) {
    return static_cast<int>(
        std::ranges::count_if(run.frames, [traversal](const FrameRecord& r) { return r.traversal == traversal; }));
}

// Skips the test when CONEY_DISC is not set; otherwise the disc's path.
const char* discOrSkip() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    return discPath;
}

} // namespace

// The first frame of `run` from `from` on whose anim id is `animId`, or -1.
int firstFrameOf(const Run& run, std::uint32_t animId, std::size_t from = 0) {
    for (std::size_t i = from; i < run.frames.size(); ++i) {
        if (run.frames[i].animId == animId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// How many frames of `run` played the clip `animId`.
int framesWithClip(const Run& run, std::uint32_t animId) {
    return static_cast<int>(
        std::ranges::count_if(run.frames, [animId](const FrameRecord& r) { return r.animId == animId; }));
}

// How far the feet moved over the ground on frame `index` of `run` (from the frame before).
float movedOn(const Run& run, int index) {
    const coney::anim::Vec3 from = run.frames[static_cast<std::size_t>(index) - 1].position;
    const coney::anim::Vec3 to = run.frames[static_cast<std::size_t>(index)].position;
    return std::hypot(to.x - from.x, to.y - from.y);
}

// The player's walking sphere (0.4704 m), and the 0.01 m the sweep leaves between it and a wall.
const float kBodyRadius = coney::human::playerWalkingRadius(coney::human::kPlayerBodyScale);
const float kContactClear = coney::human::bodyTuning().contactClear;

TEST_CASE("Rembrandt sprints along the parkour lane while L2 and stamina last", "[disc][player][sandbox][traversal]") {
    const char* discPath = discOrSkip();
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const Course course = loadCourse(discPath);
    const Run run = runScript(course, "sprint", "parkour_sprint.txt", {60.0F, -62.0F, 0.0F}, 400);
    CHECK(run.respawns == 0);
    // An 80 % stick with L2 held walks: no sprint, no drain.
    CHECK(run.frames[70].speed == Approx(1.629F).margin(0.01));
    CHECK(run.frames[70].stamina == 135);
    // A 96 % stick sprints at 10.245 m/s, and 135 stamina lasts 6.75 s at 20 a second.
    CHECK(fastest(run) == Approx(10.245F).margin(0.01));
    const auto empty = std::ranges::find_if(run.frames, [](const FrameRecord& r) { return r.stamina == 0; });
    REQUIRE(empty != run.frames.end());
    const auto emptyFrame = static_cast<std::size_t>(empty - run.frames.begin());
    const auto sprinting = std::ranges::count_if(run.frames, [](const FrameRecord& r) { return r.speed > 10.2F; });
    CHECK(sprinting >= 200);
    CHECK(sprinting <= 206);
    // Empty: the run speed at once, and no refill while L2 stays held (until frame 330).
    CHECK(run.frames[emptyFrame + 1].speed == Approx(7.801F).margin(0.01));
    for (std::size_t i = emptyFrame; i < 330; ++i) {
        CHECK(run.frames[i].stamina == 0);
    }
    // L2 let go: 40 a second at the run. The stick let go at that steady run: the speed measured there is a rounding
    // short of the run speed, so no skid and no run stop, the idle at once (docs/research/characters.md#run-stop).
    CHECK(run.frames[359].stamina >= 38);
    CHECK(run.frames[359].stamina <= 42);
    CHECK(run.frames[361].traversal == Traversal::None);
    CHECK(run.frames[362].speed == 0.0F);
    CHECK(framesOf(run, Traversal::RunStop) == 0);
}

TEST_CASE("Rembrandt jumps from a run and from a sprint on the parkour lane", "[disc][player][sandbox][traversal]") {
    const char* discPath = discOrSkip();
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const Course course = loadCourse(discPath);
    const Run run = runScript(course, "lane jump", "parkour_lane_jump.txt", {60.0F, -62.0F, 0.0F}, 240);
    CHECK(run.respawns == 0);
    // Both jumps rise 1.06 m and spend about 23 updates in the air.
    CHECK(highest(run) == Approx(1.058F).margin(0.01));
    CHECK(framesOf(run, Traversal::Jumping) >= 44);
    CHECK(framesOf(run, Traversal::Jumping) <= 50);
    // The run jump at 7.80 m/s, the sprint jump at 10.245 m/s, each landing on the jump end running (436).
    const int first = firstFrameOf(run, 434);
    REQUIRE(first >= 0);
    CHECK(run.frames[static_cast<std::size_t>(first)].speed == Approx(7.801F).margin(0.01));
    CHECK(run.frames[static_cast<std::size_t>(first)].verticalSpeed == Approx(5.5F));
    CHECK(firstFrameOf(run, 436) > first);
    const int second = firstFrameOf(run, 434, 120);
    REQUIRE(second > 0);
    CHECK(run.frames[static_cast<std::size_t>(second)].speed == Approx(10.245F).margin(0.01));
    const int secondLanding = firstFrameOf(run, 436, static_cast<std::size_t>(second));
    REQUIRE(secondLanding > second);
    // The sprint jump carries about 8.5 m: 24 updates in the air and the landing update, both at the sprint's speed.
    const float carried = run.frames[static_cast<std::size_t>(secondLanding)].position.y -
                          run.frames[static_cast<std::size_t>(second) - 1].position.y;
    CHECK(carried == Approx(8.5F).margin(0.3));
    // Let go in the sprint: the run stop (417).
    CHECK(firstFrameOf(run, 417, 200) > 0);
    CHECK(framesOf(run, Traversal::RunStop) > 0);
}

TEST_CASE("Rembrandt jumps the 5 m gap between two platforms of the parkour course",
          "[disc][player][sandbox][traversal]") {
    const char* discPath = discOrSkip();
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const Course course = loadCourse(discPath);
    // The platform from y = -0.5 to 5.5, the next from 10.5, both 2 m high.
    const Run run = runScript(course, "gap jump", "parkour_gap_jump.txt", {-80.0F, -0.3F, 2.0F}, 150);
    CHECK(run.respawns == 0);
    CHECK(framesOf(run, Traversal::Jumping) > 20);
    // The last airborne update leaves the feet below the far platform's top (0.19 m at runtime) and the next lands
    // them on it: never further down than that.
    CHECK(lowest(run) > 1.7F);
    const FrameRecord& last = run.frames.back();
    CHECK(last.position.y > 10.5F);
    CHECK(last.position.z == Approx(2.0F).margin(0.02));
    CHECK_FALSE(last.airborne);
}

TEST_CASE("Rembrandt climbs over a short fence and a fence from a run, not one of 2.6 m",
          "[disc][player][sandbox][traversal]") {
    const char* discPath = discOrSkip();
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const Course course = loadCourse(discPath);
    // The fences' faces are at y = 19.96 and their backs at 20.04.
    SECTION("a 1 m short fence: the running short-fence climb (446), through it with the feet on the ground") {
        const Run run = runScript(course, "short fence", "parkour_fence_short.txt", {-54.0F, 8.0F, 0.0F}, 150);
        CHECK(firstFrameOf(run, 446) > 0);
        CHECK(framesOf(run, Traversal::Climbing) > 20);
        CHECK(highest(run) == Approx(0.0F).margin(0.02));
        CHECK(run.frames.back().position.y > 21.0F);
        CHECK(run.respawns == 0);
    }
    SECTION("a 2 m fence: the running fence climb (440), through it") {
        const Run run = runScript(course, "fence", "parkour_fence_tall.txt", {-30.0F, 8.0F, 0.0F}, 150);
        CHECK(firstFrameOf(run, 440) > 0);
        // The original's clip lengths (docs/research/characters.md#climb): 440 runs from the tap's update through the
        // move to the start point, unfaded, for 11 updates; 441 and 442 move nothing on their first update.
        CHECK(framesWithClip(run, 440) == 11);
        CHECK(framesWithClip(run, 441) == 15);
        CHECK(framesWithClip(run, 442) == 13);
        REQUIRE(firstFrameOf(run, 441) > 0);
        REQUIRE(firstFrameOf(run, 442) > 0);
        CHECK(movedOn(run, firstFrameOf(run, 441)) == Approx(0.0F).margin(1e-4));
        CHECK(movedOn(run, firstFrameOf(run, 442)) == Approx(0.0F).margin(1e-4));
        CHECK(movedOn(run, firstFrameOf(run, 441) + 1) > 0.05F);
        CHECK(framesOf(run, Traversal::Climbing) > 20);
        CHECK(highest(run) == Approx(0.0F).margin(0.02));
        CHECK(run.frames.back().position.y > 21.0F);
        CHECK(run.respawns == 0);
    }
    SECTION("a 2.6 m fence: no climb, and no jump this close to a climbable face; the body stops at it") {
        const Run run = runScript(course, "fence 2.6", "parkour_fence_tall.txt", {-18.0F, 8.0F, 0.0F}, 150);
        CHECK(framesOf(run, Traversal::Climbing) == 0);
        CHECK(framesOf(run, Traversal::Jumping) == 0);
        CHECK(run.frames.back().position.y == Approx(19.96F - kBodyRadius - kContactClear).margin(0.03));
    }
}

TEST_CASE("Rembrandt climbs low walls and blocks standing; ledges too low or too tall stop him",
          "[disc][player][sandbox][traversal]") {
    const char* discPath = discOrSkip();
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const Course course = loadCourse(discPath);
    // The low walls' faces are at y = 34.5, the blocks' at 53.5.
    SECTION("a 1 m low wall: the standing short-wall climb (455), onto its top in one rise") {
        const Run run = runScript(course, "short wall", "parkour_climb_stand.txt", {-36.0F, 32.0F, 0.0F}, 240);
        CHECK(firstFrameOf(run, 455) > 0);
        CHECK(highest(run) == Approx(1.0F).margin(0.02));
        CHECK(run.respawns == 0);
    }
    SECTION("a 0.75 m low wall: a short wall too") {
        const Run run = runScript(course, "short wall 75", "parkour_climb_stand.txt", {-42.0F, 32.0F, 0.0F}, 240);
        CHECK(firstFrameOf(run, 455) > 0);
        CHECK(highest(run) == Approx(0.75F).margin(0.02));
    }
    SECTION("a 2 m block: the standing wall climb (449), onto its top") {
        const Run run = runScript(course, "wall", "parkour_climb_stand.txt", {-48.0F, 51.0F, 0.0F}, 240);
        CHECK(firstFrameOf(run, 449) > 0);
        CHECK(highest(run) == Approx(2.0F).margin(0.02));
        CHECK(run.frames.back().position.z == Approx(2.0F).margin(0.02));
        CHECK(run.frames.back().traversal == Traversal::None);
    }
    SECTION("a 3 m block: above the wall window, no climb") {
        const Run run = runScript(course, "block 3", "parkour_climb_stand.txt", {-30.0F, 51.0F, 0.0F}, 240);
        CHECK(framesOf(run, Traversal::Climbing) == 0);
        CHECK(run.frames.back().position.y == Approx(53.5F - kBodyRadius - kContactClear).margin(0.03));
    }
    SECTION("0.5 and 0.65 m ledges: walls to walk into, below the climbs' 0.69 m") {
        for (const float x : {-54.0F, -48.0F}) {
            const Run run = runScript(course, "ledge", "parkour_climb_stand.txt", {x, 32.0F, 0.0F}, 240);
            CHECK(framesOf(run, Traversal::Climbing) == 0);
            CHECK(highest(run) == Approx(0.0F).margin(0.02));
            CHECK(run.frames.back().position.y == Approx(34.5F - kBodyRadius - kContactClear).margin(0.03));
        }
    }
    SECTION("a 20 cm kerb is walked onto, a 30 cm one is a wall") {
        const Run kerb = runScript(course, "kerb", "parkour_kerb.txt", {-66.0F, 32.0F, 0.0F}, 150);
        CHECK(highest(kerb) == Approx(0.2F).margin(0.02));
        CHECK(kerb.frames.back().position.y > 35.0F);
        const Run ledge = runScript(course, "kerb 30", "parkour_kerb.txt", {-60.0F, 32.0F, 0.0F}, 150);
        CHECK(highest(ledge) == Approx(0.0F).margin(0.02));
        // The sphere, centred its radius + 0.05 up, meets the 30 cm face's top edge before its plane: it stops where
        // that edge is a radius from its centre, and the 0.01 m kept clear.
        const float above = kBodyRadius + 0.05F - 0.3F;
        const float edgeStop = std::sqrt((kBodyRadius * kBodyRadius) - (above * above));
        CHECK(ledge.frames.back().position.y == Approx(34.5F - edgeStop - kContactClear).margin(0.03));
    }
}
