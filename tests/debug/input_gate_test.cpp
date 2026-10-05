// SPDX-License-Identifier: GPL-3.0-or-later
// The input gate: the L3+R3 chord opens and closes the menu and never reaches the game, the open menu takes port 1
// from the game, buttons held when it closes stay hidden until released, port 2 passes, and the between-steps
// callbacks run each frame. Also the time controls, whose steps never change length.
#include "debug/input_gate.h"

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "debug/menu_model.h"
#include "debug/time_control.h"

using coney::PadSample;
using coney::PortSamples;
using coney::debug::InputGate;
using coney::debug::MenuModel;
using coney::debug::MenuNavigator;
using coney::debug::TimeControl;
namespace pad = coney::pad;

namespace {

// An input source that gives back whatever the test put in `next`.
class FakeInput final : public coney::InputSource {
  public:
    PortSamples next{};
    PortSamples sample(std::uint64_t /*frame*/) override { return next; }
};

// A connected sample holding `buttons`.
PadSample held(std::uint16_t buttons) {
    PadSample s;
    s.connected = true;
    s.buttons = buttons;
    return s;
}

} // namespace

TEST_CASE("the chord opens the menu, which then takes port 1 from the game", "[debug]") {
    MenuModel model;
    model.addPage("Page", [](coney::debug::MenuPage&) {});
    MenuNavigator nav(model);
    FakeInput source;
    InputGate gate(&source, nav);

    source.next[0] = held(pad::kL3);
    CHECK(gate.sample(0)[0].buttons == pad::kL3); // one stick press alone is gameplay's
    source.next[0] = held(pad::kL3 | pad::kR3);
    const PortSamples opened = gate.sample(1);
    CHECK(nav.isOpen());
    CHECK(opened[0].buttons == 0);
    CHECK(opened[0].connected);

    // While open, the game sees nothing; the menu moves.
    source.next[0] = held(pad::kDown);
    source.next[0].sticks[2] = 255;
    source.next[1] = held(pad::kCross);
    const PortSamples open = gate.sample(2);
    CHECK(open[0].buttons == 0);
    CHECK(open[0].sticks[2] == pad::kStickCentre);
    CHECK(open[1].buttons == pad::kCross); // port 2 passes
    CHECK(nav.cursor() == 1);
}

TEST_CASE("buttons held when the menu closes stay hidden from the game until let go", "[debug]") {
    MenuModel model;
    MenuNavigator nav(model);
    FakeInput source;
    InputGate gate(&source, nav);
    source.next[0] = held(pad::kL3 | pad::kR3);
    (void)gate.sample(0);
    REQUIRE(nav.isOpen());
    source.next[0] = held(0);
    (void)gate.sample(1);
    // Circle at the root closes the menu; the game must not see that circle, or the cross held with it.
    source.next[0] = held(pad::kCircle);
    CHECK(gate.sample(2)[0].buttons == 0);
    CHECK_FALSE(nav.isOpen());
    source.next[0] = held(pad::kCircle | pad::kCross);
    CHECK(gate.sample(3)[0].buttons == pad::kCross); // cross went down after the close: it is gameplay's
    source.next[0] = held(0);
    (void)gate.sample(4);
    source.next[0] = held(pad::kCircle);
    CHECK(gate.sample(5)[0].buttons == pad::kCircle); // released and pressed again: gameplay's
}

TEST_CASE("the chord closes the menu again and is never seen by the game", "[debug]") {
    MenuModel model;
    MenuNavigator nav(model);
    FakeInput source;
    InputGate gate(&source, nav);
    source.next[0] = held(pad::kL3 | pad::kR3);
    (void)gate.sample(0);
    source.next[0] = held(0);
    (void)gate.sample(1);
    source.next[0] = held(pad::kL3 | pad::kR3 | pad::kCross);
    CHECK(gate.sample(2)[0].buttons == 0);
    CHECK_FALSE(nav.isOpen());
}

TEST_CASE("the between-steps callbacks run once per frame, in order", "[debug]") {
    MenuModel model;
    MenuNavigator nav(model);
    InputGate gate(nullptr, nav);
    std::vector<int> calls;
    gate.addBetweenSteps([&calls] { calls.push_back(1); });
    gate.addBetweenSteps([&calls] { calls.push_back(2); });
    (void)gate.sample(0);
    (void)gate.sample(1);
    CHECK(calls == std::vector<int>{1, 2, 1, 2});
}

TEST_CASE("time control pauses, steps one at a time and runs slow motion as fewer whole steps", "[debug]") {
    TimeControl time;
    CHECK(time.shouldStep());
    time.setPaused(true);
    CHECK_FALSE(time.shouldStep());
    time.stepOnce();
    time.stepOnce();
    CHECK(time.shouldStep());
    CHECK(time.shouldStep());
    CHECK_FALSE(time.shouldStep());
    time.setPaused(false);
    time.setSlowMotion(3);
    std::vector<bool> pattern;
    pattern.reserve(6);
    for (int i = 0; i < 6; ++i) {
        pattern.push_back(time.shouldStep());
    }
    CHECK(pattern == std::vector<bool>{true, false, false, true, false, false});
    CHECK(time.frames() == 11);
    CHECK(time.steps() == 5);
    time.setSlowMotion(100);
    CHECK(time.slowMotion() == TimeControl::kMaxDivisor);
}
