// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/game_mode_stack.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/frame_clock.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "gamemodes/game_mode.h"

using coney::FrameTime;
using coney::GameMode;
using coney::GameModeStack;
using coney::GameTimer;
using coney::ModeResult;

namespace {

/// A mode that records every call into a shared log and leaves after a set number of updates.
class LoggingMode final : public GameMode {
  public:
    LoggingMode(std::string name, std::uint32_t id, std::vector<std::string>& log, int updatesBeforeLeaving)
        : m_name(std::move(name)), m_id(id), m_log(log), m_left(updatesBeforeLeaving) {}

    [[nodiscard]] std::uint32_t id() const override { return m_id; }
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override {
        m_log.push_back(m_name + ".update");
        seconds.push_back(frame.seconds);
        if (pushOnFirstUpdate != nullptr) {
            stack.push(*pushOnFirstUpdate);
            pushOnFirstUpdate = nullptr;
        }
        return --m_left <= 0 ? ModeResult::Leave : ModeResult::Stay;
    }
    void render(const coney::RenderTime& time) override {
        m_log.push_back(m_name + ".render");
        alphas.push_back(time.alpha);
    }
    void enter() override { m_log.push_back(m_name + ".enter"); }
    void exit() override { m_log.push_back(m_name + ".exit"); }
    void resume() override { m_log.push_back(m_name + ".resume"); }
    void suspend() override { m_log.push_back(m_name + ".suspend"); }

    GameMode* pushOnFirstUpdate = nullptr; ///< Pushed onto the stack during the next update, then cleared.
    std::vector<double> seconds;           ///< The step each update was given.
    std::vector<float> alphas;             ///< The alpha each render was given.

  private:
    std::string m_name;
    std::uint32_t m_id;
    std::vector<std::string>& m_log;
    int m_left;
};

} // namespace

TEST_CASE("an empty stack has no top and id 0", "[game_mode_stack]") {
    GameModeStack stack;
    CHECK(stack.empty());
    CHECK(stack.top() == nullptr);
    CHECK(stack.topId() == 0);
    stack.step(FrameTime{});
}

TEST_CASE("modes are entered on their first step, suspended, resumed and exited", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode bottom("bottom", 8, log, 2);
    LoggingMode top("top", 5, log, 1);
    GameModeStack stack;
    stack.push(bottom);
    CHECK(log.empty()); // pushing does not enter
    stack.step(FrameTime{});
    stack.push(top);
    CHECK(stack.topId() == 5);
    stack.step(FrameTime{}); // top enters, runs and leaves
    stack.step(FrameTime{}); // bottom runs its second update and leaves
    CHECK(stack.empty());
    CHECK(log == std::vector<std::string>{"bottom.enter", "bottom.update", "bottom.suspend", "top.enter", "top.update",
                                          "top.exit", "bottom.resume", "bottom.update", "bottom.exit"});
    CHECK_FALSE(bottom.entered());
}

TEST_CASE("a mode pushed before the one below was entered does not suspend it", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode bottom("bottom", 8, log, 1);
    LoggingMode top("top", 5, log, 1);
    GameModeStack stack;
    stack.push(bottom);
    stack.push(top);
    stack.step(FrameTime{});
    CHECK(log == std::vector<std::string>{"top.enter", "top.update", "top.exit"});
    stack.step(FrameTime{});
    CHECK(log.back() == "bottom.exit");
}

TEST_CASE("leaving after pushing pops the pushed mode, as in the original", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode bottom("bottom", 8, log, 1);
    LoggingMode pushed("pushed", 6, log, 1);
    bottom.pushOnFirstUpdate = &pushed;
    GameModeStack stack;
    stack.push(bottom);
    stack.step(FrameTime{});
    CHECK(stack.topId() == 8);
    CHECK(log == std::vector<std::string>{"bottom.enter", "bottom.update", "bottom.suspend", "bottom.resume"});
}

TEST_CASE("the run loop steps at exactly 1/30 s until the stack is empty", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode mode("mode", 8, log, 3);
    GameModeStack stack;
    stack.push(mode);
    GameTimer timer;
    const std::uint64_t frames = stack.runUntilEmpty(timer, {}, std::nullopt);
    CHECK(frames == 3);
    CHECK(stack.empty());
    CHECK(timer.ticks() == 3 * GameTimer::kFixedStepTicks);
    for (const double seconds : mode.seconds) {
        CHECK(seconds == GameTimer::toSeconds(GameTimer::kFixedStepTicks));
    }
}

TEST_CASE("the run loop stops at the frame limit or when the frame hook says so", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode mode("mode", 8, log, 1000);
    GameModeStack stack;
    stack.push(mode);
    GameTimer timer;
    CHECK(stack.runUntilEmpty(timer, {}, 4) == 4);
    int hooks = 0;
    const auto hook = [&hooks] {
        ++hooks;
        return hooks < 3;
    };
    CHECK(stack.runUntilEmpty(timer, hook, std::nullopt) == 2);
    CHECK(stack.size() == 1);
}

namespace {

/// A mode that records what port 1's record says was pressed in each update, and never leaves.
class PadReadingMode final : public GameMode {
  public:
    [[nodiscard]] std::uint32_t id() const override { return 0x100; }
    ModeResult update(GameModeStack& stack, const FrameTime& /*frame*/) override {
        pressed.push_back(stack.pads().port(0).pressed());
        return ModeResult::Stay;
    }
    std::vector<std::uint16_t> pressed; ///< pressed() of port 1, one entry per update.
};

} // namespace

TEST_CASE("the run loop samples the pads from its input source before each update", "[game_mode_stack]") {
    auto events = coney::parseInputScript("1 tap start\n3 press cross\n");
    REQUIRE(events.has_value());
    coney::ScriptedInput input(std::move(*events));
    PadReadingMode mode;
    GameModeStack stack;
    stack.push(mode);
    stack.setInput(&input);
    GameTimer timer;
    stack.runUntilEmpty(timer, {}, 5);
    CHECK(mode.pressed == std::vector<std::uint16_t>{0, coney::pad::kStart, 0, coney::pad::kCross, 0});
    CHECK(stack.pads().port(0).held(coney::pad::kCross));
}

TEST_CASE("without an input source the pads stay disconnected", "[game_mode_stack]") {
    PadReadingMode mode;
    GameModeStack stack;
    stack.push(mode);
    GameTimer timer;
    stack.runUntilEmpty(timer, {}, 2);
    CHECK_FALSE(stack.pads().port(0).connected());
    CHECK(mode.pressed == std::vector<std::uint16_t>{0, 0});
}

namespace {

/// Hooks that feed the loop made-up frame times: the first frame at time 0, then one frame per entry of `times`
/// (nanoseconds), stopping when they run out.
struct FakeFrames {
    std::vector<std::uint64_t> times;
    std::size_t next = 0; // frames begun

    coney::FrameHooks hooks() {
        coney::FrameHooks hooks;
        hooks.waitForFrame = [this] {
            return next == 0 || next > times.size() ? std::uint64_t{0} : times.at(next - 1);
        };
        hooks.beginFrame = [this] { return ++next <= times.size() + 1; };
        return hooks;
    }
};

constexpr std::uint64_t kStepNs = 33'333'334; // a little over 1/30 s: one step each

} // namespace

TEST_CASE("in lockstep every frame is one update and then one render at alpha 1", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode mode("mode", 8, log, 2);
    GameModeStack stack;
    stack.push(mode);
    GameTimer timer;
    CHECK(stack.runUntilEmpty(timer, {}, std::nullopt) == 2);
    // The step it leaves on is still drawn, before it is popped: the original's update drew that frame.
    CHECK(log == std::vector<std::string>{"mode.enter", "mode.update", "mode.render", "mode.update", "mode.render",
                                          "mode.exit"});
    CHECK(mode.alphas == std::vector<float>{1.0F, 1.0F});
}

TEST_CASE("a frame may run several steps or none, and renders once either way", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode mode("mode", 8, log, 1000);
    GameModeStack stack;
    stack.push(mode);
    GameTimer timer;
    coney::FrameClock clock(coney::FramePacing::Interpolated);
    // The first frame (one step), a frame of three steps' time, then two frames of a fifth of a step each.
    FakeFrames frames{{3 * kStepNs, kStepNs / 5, kStepNs / 5}};
    const coney::LoopCounts counts = stack.runUntilEmpty(timer, clock, frames.hooks(), std::nullopt);
    CHECK(counts.frames == 4);
    CHECK(counts.steps == 4);
    CHECK(log == std::vector<std::string>{"mode.enter", "mode.update", "mode.render", "mode.update", "mode.update",
                                          "mode.update", "mode.render", "mode.render", "mode.render"});
    REQUIRE(mode.alphas.size() == 4);
    // The renders without a step fall further between the last two steps.
    CHECK(mode.alphas[2] > mode.alphas[1]);
    CHECK(mode.alphas[3] > mode.alphas[2]);
    CHECK(mode.alphas[3] < 1.0F);
}

TEST_CASE("a mode leaving on an earlier step of a frame is popped before the next step", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode bottom("bottom", 8, log, 1000);
    LoggingMode top("top", 5, log, 2);
    GameModeStack stack;
    stack.push(bottom);
    stack.push(top);
    GameTimer timer;
    coney::FrameClock clock(coney::FramePacing::Interpolated);
    FakeFrames frames{{3 * kStepNs}};
    stack.runUntilEmpty(timer, clock, frames.hooks(), std::nullopt);
    CHECK(log == std::vector<std::string>{"top.enter", "top.update", "top.render", "top.update", "top.exit",
                                          "bottom.enter", "bottom.update", "bottom.update", "bottom.render"});
}

TEST_CASE("a mode that pushes another is still the one drawn until the new one has run", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode bottom("bottom", 8, log, 1000);
    LoggingMode pushed("pushed", 6, log, 1000);
    bottom.pushOnFirstUpdate = &pushed;
    GameModeStack stack;
    stack.push(bottom);
    GameTimer timer;
    stack.runUntilEmpty(timer, {}, 2);
    CHECK(log == std::vector<std::string>{"bottom.enter", "bottom.update", "bottom.suspend", "bottom.render",
                                          "pushed.enter", "pushed.update", "pushed.render"});
}

TEST_CASE("the frame limit counts steps", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode mode("mode", 8, log, 1000);
    GameModeStack stack;
    stack.push(mode);
    GameTimer timer;
    coney::FrameClock clock(coney::FramePacing::Interpolated);
    FakeFrames frames{{4 * kStepNs, 4 * kStepNs}};
    const coney::LoopCounts counts = stack.runUntilEmpty(timer, clock, frames.hooks(), 6);
    CHECK(counts.steps == 6);
    CHECK(timer.ticks() == 6 * GameTimer::kFixedStepTicks);
}

namespace {

// An input source that only notes which sample indexes it was asked for.
class CountingInput final : public coney::InputSource {
  public:
    [[nodiscard]] coney::PortSamples sample(std::uint64_t frame) override {
        asked.push_back(frame);
        return {};
    }
    std::vector<std::uint64_t> asked;
};

} // namespace

TEST_CASE("a step gate holds steps: the pads are read, but no update runs and no game time passes",
          "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode mode("a", 1, log, 100);
    GameModeStack stack;
    stack.push(mode);
    CountingInput input;
    stack.setInput(&input);
    // Every other step runs, as half-speed slow motion does.
    bool run = true;
    stack.setStepGate([&run] {
        const bool now = run;
        run = !run;
        return now;
    });
    GameTimer timer;
    // The limit counts held steps too, so a paused run still ends.
    CHECK(stack.runUntilEmpty(timer, {}, 6) == 3);
    CHECK(mode.seconds.size() == 3);
    CHECK(timer.ticks() == 3 * GameTimer::kFixedStepTicks); // whole fixed steps only
    CHECK(input.asked == std::vector<std::uint64_t>{0, 1, 2, 3, 4, 5});
    CHECK(mode.alphas == std::vector<float>(6, 1.0F));
}

TEST_CASE("while the gate holds, the render shows the newest step and does not drift", "[game_mode_stack]") {
    std::vector<std::string> log;
    LoggingMode mode("a", 1, log, 100);
    GameModeStack stack;
    stack.push(mode);
    // The first step runs, then the game is paused.
    bool first = true;
    stack.setStepGate([&first] { return std::exchange(first, false); });
    GameTimer timer;
    coney::FrameClock clock(coney::FramePacing::Interpolated);
    FakeFrames frames{{kStepNs, kStepNs / 5, kStepNs / 5}};
    const coney::LoopCounts counts = stack.runUntilEmpty(timer, clock, frames.hooks(), std::nullopt);
    CHECK(counts.steps == 1);
    CHECK(counts.held == 1);
    // The first frame blends as any frame does; every frame after the held step shows the newest step as it is.
    REQUIRE(mode.alphas.size() == 4);
    CHECK(std::vector<float>(mode.alphas.begin() + 1, mode.alphas.end()) == std::vector<float>(3, 1.0F));
}
