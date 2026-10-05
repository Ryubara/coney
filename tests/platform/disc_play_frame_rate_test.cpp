// SPDX-License-Identifier: GPL-3.0-or-later

// The frame-rate guarantee with the real game: `--play-level level99` (Rembrandt, the follow camera and the streamed
// scenery) driven through the main loop by the scripted analog pads of tests/support, at several display rates and
// with irregular frames over the same real time, must leave the player, the camera and the streaming bit for bit as
// test mode (lockstep) leaves them. It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise; it prints counts and hashes only, never data (LEGAL.md).

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "core/frame_clock.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "world/sector_budget.h"

namespace {

constexpr std::uint64_t kSecond = coney::FrameClock::kNanosecondsPerSecond;
// Long enough for both scripts to end with Rembrandt standing again.
constexpr std::uint64_t kSeconds = 13;

/// FNV-1a over 32-bit words.
class Hash {
  public:
    // Mixes one word in.
    void mix(std::uint32_t word) {
        for (unsigned i = 0; i < 4; ++i) {
            m_value = (m_value ^ ((word >> (8U * i)) & 0xffU)) * 1099511628211ULL;
        }
    }
    // Mixes a float in, bit for bit.
    void mix(float value) { mix(std::bit_cast<std::uint32_t>(value)); }
    // Mixes a vector in.
    void mix(coney::anim::Vec3 v) {
        mix(v.x);
        mix(v.y);
        mix(v.z);
    }
    [[nodiscard]] std::uint64_t value() const { return m_value; }

  private:
    std::uint64_t m_value = 14695981039346656037ULL;
};

/// The newest step's snapshot (feet, heading, every bone of the pose, camera eye and target), bit for bit.
std::uint64_t hashOf(const coney::human::PlayerSnapshot& snapshot) {
    Hash hash;
    hash.mix(snapshot.feet);
    hash.mix(snapshot.heading);
    hash.mix(snapshot.cameraEye);
    hash.mix(snapshot.cameraTarget);
    hash.mix(snapshot.pose.rootTranslation);
    for (const coney::anim::Quat& q : snapshot.pose.rotations) {
        hash.mix(q.x);
        hash.mix(q.y);
        hash.mix(q.z);
        hash.mix(q.w);
    }
    return hash.value();
}

/// What one run left.
struct PlayRun {
    std::uint64_t frames = 0;
    std::uint64_t steps = 0;
    std::uint64_t ticks = 0;
    std::uint64_t snapshot = 0; ///< hashOf() the newest snapshot.
    std::string summary;        ///< The mode's summary line: position, heading, speed, clip, streaming counts.
};

/// Plays level99 under `script` (in tests/support): in lockstep for `stepLimit` steps when `times` is empty,
/// otherwise interpolated, one frame per entry of `times` after the first at time 0.
PlayRun play(coney::platform::RenderEngine& engine, const coney::io::Wad& wad, const std::string& script,
             const std::vector<std::uint64_t>& times, std::optional<std::uint64_t> stepLimit) {
    const std::filesystem::path path = std::filesystem::path(CONEY_TEST_SUPPORT_DIR) / script;
    auto events = coney::loadInputScript(path.string());
    REQUIRE(events.has_value());
    coney::ScriptedInput input(std::move(*events));
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    auto mode = coney::platform::PlayLevelMode::create(engine, wad, "level99", budget, [](std::string_view) {});
    REQUIRE(mode.has_value());
    coney::GameModeStack stack;
    stack.push(**mode);
    stack.setInput(&input);
    coney::GameTimer timer;
    coney::FrameClock clock(times.empty() ? coney::FramePacing::Lockstep : coney::FramePacing::Interpolated);
    std::size_t next = 0;
    coney::FrameHooks hooks;
    if (!times.empty()) {
        hooks.waitForFrame = [&times, &next] {
            return next == 0 || next > times.size() ? std::uint64_t{0} : times.at(next - 1);
        };
        hooks.beginFrame = [&times, &next] { return ++next <= times.size() + 1; };
    }
    const coney::LoopCounts counts = stack.runUntilEmpty(timer, clock, hooks, stepLimit);
    return PlayRun{counts.frames, counts.steps, timer.ticks(), hashOf((*mode)->player().current()), (*mode)->summary()};
}

/// `seconds` of frames at `hz`, each ending at floor(k × 10^9 / hz) ns, so they add up to exactly `seconds`.
std::vector<std::uint64_t> framesAt(std::uint64_t hz, std::uint64_t seconds) {
    std::vector<std::uint64_t> times;
    std::uint64_t last = 0;
    for (std::uint64_t k = 1; k <= hz * seconds; ++k) {
        times.push_back((k * kSecond / hz) - last);
        last = k * kSecond / hz;
    }
    return times;
}

/// `seconds` of irregular frames between 2 and 70 ms, adding up exactly.
std::vector<std::uint64_t> jitteryFrames(std::uint64_t seconds) {
    std::vector<std::uint64_t> times;
    std::uint64_t total = 0;
    std::uint64_t state = 424242;
    while (total < seconds * kSecond) {
        state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
        const std::uint64_t length = std::min(2'000'000 + ((state >> 33U) % 68'000'000), (seconds * kSecond) - total);
        times.push_back(length);
        total += length;
    }
    return times;
}

} // namespace

TEST_CASE("playing level99 gives the same player, camera and streaming at every frame rate",
          "[disc][player][frame_rate]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());

    const std::string script = GENERATE(as<std::string>{}, "play_walk.txt", "play_wall.txt");
    // The reference: test mode, the first frame's step and then 30 a second.
    const std::uint64_t steps = 1 + (30 * kSeconds);
    const PlayRun lockstep = play(**engine, *wad, script, {}, steps);
    REQUIRE(lockstep.steps == steps);
    std::printf("%s lockstep: %llu steps, snapshot %#018llx\n", script.c_str(),
                static_cast<unsigned long long>(lockstep.steps), static_cast<unsigned long long>(lockstep.snapshot));

    for (const std::uint64_t hz : {30ULL, 60ULL, 144ULL, 240ULL, 1000ULL, 0ULL}) {
        // 0 stands for the irregular frames.
        const std::vector<std::uint64_t> times = hz == 0 ? jitteryFrames(kSeconds) : framesAt(hz, kSeconds);
        const PlayRun paced = play(**engine, *wad, script, times, std::nullopt);
        std::printf("%s %s: %llu frames, %llu steps, snapshot %#018llx\n", script.c_str(),
                    hz == 0 ? "jitter" : std::to_string(hz).append(" Hz").c_str(),
                    static_cast<unsigned long long>(paced.frames), static_cast<unsigned long long>(paced.steps),
                    static_cast<unsigned long long>(paced.snapshot));
        CAPTURE(script, hz);
        CHECK(paced.steps == lockstep.steps);
        CHECK(paced.ticks == lockstep.ticks);
        CHECK(paced.snapshot == lockstep.snapshot);
        CHECK(paced.summary == lockstep.summary);
    }
}
