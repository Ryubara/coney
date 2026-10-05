// SPDX-License-Identifier: GPL-3.0-or-later

// The guarantee that the game runs the same at any frame rate: the main loop is driven through the frame clock with
// made-up frame times at several display rates and with irregular frames, all over the same real time and with the
// same scripted pad, and the simulation must come out bit for bit the same as in lockstep (test mode).

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "core/frame_clock.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/interpolation.h"
#include "core/pad.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/game_mode_stack.h"

using coney::FrameClock;
using coney::FramePacing;
using coney::GameModeStack;
using coney::GameTimer;

namespace {

constexpr std::uint64_t kSecond = FrameClock::kNanosecondsPerSecond;

/// A walker on a plane, driven like a player: the left stick moves it at up to kSpeed in the direction it faces, the
/// right stick turns it, and cross makes it hop (counted). The state is integrated over each step's time, so any
/// difference in the steps the loop runs shows in its hash.
struct Walker {
    static constexpr float kSpeed = 4.0F;
    static constexpr float kTurnRate = 2.0F;

    float x = 0.0F;
    float y = 0.0F;
    float yaw = 0.0F;
    std::uint32_t hops = 0;
};

/// FNV-1a over a walker's fields, bit for bit.
std::uint64_t hashOf(const Walker& walker) {
    std::uint64_t hash = 14695981039346656037ULL;
    // Mixes one 32-bit word into the hash.
    const auto mix = [&hash](std::uint32_t word) {
        for (int i = 0; i < 4; ++i) {
            hash = (hash ^ ((word >> (8U * static_cast<unsigned>(i))) & 0xffU)) * 1099511628211ULL;
        }
    };
    mix(std::bit_cast<std::uint32_t>(walker.x));
    mix(std::bit_cast<std::uint32_t>(walker.y));
    mix(std::bit_cast<std::uint32_t>(walker.yaw));
    mix(walker.hops);
    return hash;
}

/// A test mode that integrates the walker from the pad each step and blends it for each render. Its render checks
/// that it changes nothing the simulation reads.
class WalkerMode final : public coney::GameMode {
  public:
    [[nodiscard]] std::uint32_t id() const override { return 0x1f0; }

    coney::ModeResult update(GameModeStack& stack, const coney::FrameTime& frame) override {
        m_walker.commit();
        Walker& walker = m_walker.current();
        const coney::Pad& pad = stack.pads().port(0);
        const auto seconds = static_cast<float>(frame.seconds);
        walker.yaw = std::remainder(walker.yaw - (pad.rightX() * Walker::kTurnRate * seconds), 6.2831853F);
        walker.x += std::cos(walker.yaw) * pad.leftY() * Walker::kSpeed * seconds;
        walker.y += std::sin(walker.yaw) * pad.leftY() * Walker::kSpeed * seconds;
        walker.x += std::sin(walker.yaw) * pad.leftX() * Walker::kSpeed * seconds;
        if (pad.pressed(coney::pad::kCross)) {
            ++walker.hops;
        }
        return coney::ModeResult::Stay;
    }

    void render(const coney::RenderTime& time) override {
        const std::uint64_t before = hashOf(m_walker.current());
        const Walker& from = m_walker.previous();
        const Walker& to = m_walker.current();
        // What a renderer would draw: the blended position and heading.
        lastDrawn = {coney::lerp(from.x, to.x, time.alpha), coney::lerp(from.y, to.y, time.alpha),
                     coney::lerpAngle(from.yaw, to.yaw, time.alpha), to.hops};
        minAlpha = std::min(minAlpha, time.alpha);
        maxAlpha = std::max(maxAlpha, time.alpha);
        renderChangedState = renderChangedState || hashOf(m_walker.current()) != before;
    }

    [[nodiscard]] const Walker& walker() const { return m_walker.current(); }

    Walker lastDrawn;
    float minAlpha = 1.0F;
    float maxAlpha = 0.0F;
    bool renderChangedState = false;

  private:
    coney::Interpolated<Walker> m_walker{Walker{}};
};

/// The pad a player gives over the run: partial stick deflections that change over time, and taps of cross.
constexpr std::string_view kScript = "0 stick left 0 40\n"
                                     "30 stick right 35 0\n"
                                     "45 tap cross\n"
                                     "60 stick left -25 70\n"
                                     "90 stick right -60 0\n"
                                     "120 tap cross\n"
                                     "150 stick left 55 -30\n"
                                     "151 tap cross\n"
                                     "200 stick right 0 0\n"
                                     "240 stick left 0 100\n"
                                     "300 stick left 0 0\n"
                                     "301 tap cross\n";

/// What one run gave.
struct RunResult {
    std::uint64_t frames = 0;
    std::uint64_t steps = 0;
    std::uint64_t ticks = 0;
    std::uint64_t hash = 0;
    std::uint32_t hops = 0;
    float minAlpha = 0.0F;
    float maxAlpha = 0.0F;
    bool renderChangedState = false;
};

/// Runs a walker under `pacing`, one frame per entry of `times` after the first at time 0 (or `stepLimit` steps in
/// lockstep, where the times are not read), with the scripted pad.
RunResult run(FramePacing pacing, const std::vector<std::uint64_t>& times, std::optional<std::uint64_t> stepLimit) {
    auto events = coney::parseInputScript(kScript);
    REQUIRE(events.has_value());
    coney::ScriptedInput input(std::move(*events));
    WalkerMode mode;
    GameModeStack stack;
    stack.push(mode);
    stack.setInput(&input);
    GameTimer timer;
    FrameClock clock(pacing);
    std::size_t next = 0;
    coney::FrameHooks hooks;
    if (pacing == FramePacing::Interpolated) {
        hooks.waitForFrame = [&times, &next] {
            return next == 0 || next > times.size() ? std::uint64_t{0} : times.at(next - 1);
        };
        hooks.beginFrame = [&times, &next] { return ++next <= times.size() + 1; };
    }
    const coney::LoopCounts counts = stack.runUntilEmpty(timer, clock, hooks, stepLimit);
    return RunResult{counts.frames,      counts.steps,  timer.ticks(), hashOf(mode.walker()),
                     mode.walker().hops, mode.minAlpha, mode.maxAlpha, mode.renderChangedState};
}

/// `seconds` of frames at `hz`, each frame ending at floor(k × 10^9 / hz) ns, so they add up to exactly `seconds`.
std::vector<std::uint64_t> framesAt(std::uint64_t hz, std::uint64_t seconds) {
    std::vector<std::uint64_t> times;
    std::uint64_t last = 0;
    for (std::uint64_t k = 1; k <= hz * seconds; ++k) {
        times.push_back((k * kSecond / hz) - last);
        last = k * kSecond / hz;
    }
    return times;
}

/// `seconds` of irregular frames between 2 and 60 ms (some longer than a step, many shorter), adding up exactly.
std::vector<std::uint64_t> jitteryFrames(std::uint64_t seconds) {
    std::vector<std::uint64_t> times;
    std::uint64_t total = 0;
    std::uint64_t state = 987654321;
    while (total < seconds * kSecond) {
        state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
        const std::uint64_t length = std::min(2'000'000 + ((state >> 33U) % 58'000'000), (seconds * kSecond) - total);
        times.push_back(length);
        total += length;
    }
    return times;
}

constexpr std::uint64_t kSeconds = 12;
// The first frame's step, then 30 a second.
constexpr std::uint64_t kSteps = 1 + (30 * kSeconds);

} // namespace

TEST_CASE("the simulation is bit for bit the same at every frame rate", "[frame_rate]") {
    // The reference: test mode, one step per frame, no clock.
    const RunResult lockstep = run(FramePacing::Lockstep, {}, kSteps);
    REQUIRE(lockstep.steps == kSteps);
    CHECK(lockstep.hops == 4);
    UNSCOPED_INFO("lockstep: " << lockstep.steps << " steps, hash " << lockstep.hash);

    const std::uint64_t hz = GENERATE(30, 60, 144, 240, 1000, 0);
    // 0 stands for the irregular frames.
    const std::vector<std::uint64_t> times = hz == 0 ? jitteryFrames(kSeconds) : framesAt(hz, kSeconds);
    const RunResult paced = run(FramePacing::Interpolated, times, std::nullopt);
    CAPTURE(hz, paced.frames, paced.hash);
    CHECK(paced.frames == times.size() + 1);
    CHECK(paced.steps == lockstep.steps);
    CHECK(paced.ticks == lockstep.ticks);
    CHECK(paced.ticks == kSteps * GameTimer::kFixedStepTicks);
    CHECK(paced.hash == lockstep.hash);
    CHECK(paced.hops == lockstep.hops);
    CHECK(paced.minAlpha >= 0.0F);
    CHECK(paced.maxAlpha < 1.0F);
    CHECK(!paced.renderChangedState);
}

TEST_CASE("a render at alpha 1 draws exactly the newest state", "[frame_rate]") {
    // Lockstep renders at alpha 1, so what is drawn is the simulation's own state, bit for bit.
    auto events = coney::parseInputScript(kScript);
    REQUIRE(events.has_value());
    coney::ScriptedInput input(std::move(*events));
    WalkerMode mode;
    GameModeStack stack;
    stack.push(mode);
    stack.setInput(&input);
    GameTimer timer;
    stack.runUntilEmpty(timer, {}, 100);
    CHECK(hashOf(mode.lastDrawn) == hashOf(mode.walker()));
    CHECK(mode.minAlpha == 1.0F);
}
