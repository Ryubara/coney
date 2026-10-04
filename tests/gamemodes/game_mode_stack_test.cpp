// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/game_mode_stack.h"

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_timer.h"
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
    void enter() override { m_log.push_back(m_name + ".enter"); }
    void exit() override { m_log.push_back(m_name + ".exit"); }
    void resume() override { m_log.push_back(m_name + ".resume"); }
    void suspend() override { m_log.push_back(m_name + ".suspend"); }

    GameMode* pushOnFirstUpdate = nullptr;
    std::vector<double> seconds;

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
    CHECK(stack.runUntilEmpty(timer, [&hooks] { return ++hooks < 3; }, std::nullopt) == 2);
    CHECK(stack.size() == 1);
}
