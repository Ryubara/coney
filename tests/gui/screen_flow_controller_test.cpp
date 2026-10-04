// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/screen_flow_controller.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::gui::ScreenFlowController;
using coney::gui::ScreenFlowState;

namespace {

// A screen that logs its calls and returns the results queued for it, then kStay.
class Screen final : public ScreenFlowState {
  public:
    Screen(std::string name, std::vector<std::string>& log) : m_name(std::move(name)), m_log(log) {}
    [[nodiscard]] std::string_view name() const override { return m_name; }
    void enter(ScreenFlowController& /*flow*/) override { m_log.push_back(m_name + ".enter"); }
    int update() override {
        m_log.push_back(m_name + ".update");
        if (results.empty()) {
            return kStay;
        }
        const int result = results.front();
        results.erase(results.begin());
        return result;
    }
    void exit() override { m_log.push_back(m_name + ".exit"); }

    std::vector<int> results; ///< What the next updates return, first first.

  private:
    std::string m_name;
    std::vector<std::string>& m_log;
};

} // namespace

TEST_CASE("screen flow: an empty flow is done", "[screen_flow]") {
    ScreenFlowController flow;
    CHECK(flow.update());
    CHECK(flow.top() == nullptr);
}

TEST_CASE("screen flow: a code with a transition pushes the next screen, exiting the covered one", "[screen_flow]") {
    std::vector<std::string> log;
    Screen a("a", log);
    Screen b("b", log);
    ScreenFlowController::addTransition(a, 0, b);
    ScreenFlowController flow;
    flow.push(a);
    a.results = {0};
    CHECK_FALSE(flow.update());
    CHECK(flow.top() == &b);
    CHECK(flow.size() == 2);
    CHECK(log == std::vector<std::string>{"a.enter", "a.update", "a.exit", "b.enter"});
}

TEST_CASE("screen flow: stay keeps the screen and a code without a transition is ignored", "[screen_flow]") {
    std::vector<std::string> log;
    Screen a("a", log);
    ScreenFlowController flow;
    flow.push(a);
    a.results = {ScreenFlowState::kStay, 7};
    CHECK_FALSE(flow.update());
    CHECK_FALSE(flow.update());
    CHECK(flow.top() == &a);
    CHECK(log == std::vector<std::string>{"a.enter", "a.update", "a.update"});
}

TEST_CASE("screen flow: back pops the screen and re-enters the one below; the last pop ends the flow",
          "[screen_flow]") {
    std::vector<std::string> log;
    Screen a("a", log);
    Screen b("b", log);
    ScreenFlowController flow;
    flow.push(a);
    flow.push(b);
    log.clear();
    b.results = {ScreenFlowState::kBack};
    CHECK_FALSE(flow.update());
    CHECK(log == std::vector<std::string>{"b.update", "b.exit", "a.enter"});
    a.results = {ScreenFlowState::kBack};
    CHECK(flow.update());
    CHECK(flow.empty());
}

TEST_CASE("screen flow: a transition to a screen on the stack unwinds to it", "[screen_flow]") {
    std::vector<std::string> log;
    Screen a("a", log);
    Screen b("b", log);
    Screen c("c", log);
    ScreenFlowController::addTransition(a, 0, b);
    ScreenFlowController::addTransition(b, 0, c);
    ScreenFlowController::addTransition(c, 8, a);
    ScreenFlowController flow;
    flow.push(a);
    a.results = {0};
    flow.update();
    b.results = {0};
    flow.update();
    REQUIRE(flow.size() == 3);
    log.clear();
    c.results = {8};
    CHECK_FALSE(flow.update());
    CHECK(flow.size() == 1);
    CHECK(flow.top() == &a);
    // Only the top was entered, so only it is exited; then the target is entered.
    CHECK(log == std::vector<std::string>{"c.update", "c.exit", "a.enter"});
}

TEST_CASE("screen flow: a screen's transition to itself re-enters it", "[screen_flow]") {
    std::vector<std::string> log;
    Screen a("a", log);
    ScreenFlowController::addTransition(a, 4, a);
    ScreenFlowController flow;
    flow.push(a);
    log.clear();
    a.results = {4};
    flow.update();
    CHECK(flow.size() == 1);
    CHECK(log == std::vector<std::string>{"a.update", "a.exit", "a.enter"});
}

TEST_CASE("screen flow: clear exits the top and empties the stack", "[screen_flow]") {
    std::vector<std::string> log;
    Screen a("a", log);
    Screen b("b", log);
    ScreenFlowController flow;
    flow.push(a);
    flow.push(b);
    log.clear();
    flow.clear();
    CHECK(flow.empty());
    CHECK(log == std::vector<std::string>{"b.exit"});
    CHECK(a.transition(0) == nullptr);
}
