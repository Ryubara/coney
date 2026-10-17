// Trigger kinds over synthetic button words (research/pages/pad-input/page.md, Trigger kinds and Command table).

#include "game/pad.hpp"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <optional>
#include <vector>

using namespace coney::game;

namespace {

// Feeds one button word per update and keeps what each update carried.
struct Pad {
    std::vector<CommandBinding> bindings;
    PlayerPad pad;

    std::optional<int> step(ButtonWord word) { return update_pad_commands(pad, word, bindings); }

    // The commands carried while `word` is down for `updates` updates, then the one carried on the update it is
    // let go.
    std::vector<std::optional<int>> hold(ButtonWord word, int updates) {
        std::vector<std::optional<int>> out;
        for (int i = 0; i < updates; ++i) {
            out.push_back(step(word));
        }
        out.push_back(step(0));
        return out;
    }

    // The 1-based updates on which `command` was carried while holding `word`, and the release update.
    std::vector<int> updates_carrying(ButtonWord word, int updates, int command) {
        std::vector<int> out;
        const auto carried = hold(word, updates);
        for (std::size_t i = 0; i < carried.size(); ++i) {
            if (carried[i] == command) {
                out.push_back(static_cast<int>(i) + 1);
            }
        }
        return out;
    }
};

constexpr int kCmd = 1;
Pad single(TriggerKind kind, ButtonWord buttons, ButtonWord second = 0) {
    return Pad{.bindings = {{kCmd, kind, buttons, second}}, .pad = {}};
}

using Updates = std::vector<int>;

} // namespace

TEST_CASE("held matches on every update the button is down") {
    auto pad = single(TriggerKind::Held, button::kSquare);
    CHECK(pad.updates_carrying(button::kSquare, 4, kCmd) == Updates{1, 2, 3, 4});
}

TEST_CASE("pressed matches on the update the button goes down") {
    auto pad = single(TriggerKind::Pressed, button::kSquare);
    CHECK(pad.updates_carrying(button::kSquare, 4, kCmd) == Updates{1});
    CHECK(pad.updates_carrying(button::kSquare, 1, kCmd) == Updates{1});
}

TEST_CASE("released matches on the update the button comes up") {
    auto pad = single(TriggerKind::Released, button::kR2);
    CHECK(pad.updates_carrying(button::kR2, 3, kCmd) == Updates{4});
    CHECK(pad.step(0) == std::nullopt);
}

TEST_CASE("repeat matches on the press, and a held d-pad direction auto-repeats on 15, 19, 23") {
    auto pad = single(TriggerKind::Repeat, button::kDpadDown);
    CHECK(pad.updates_carrying(button::kDpadDown, 24, kCmd) == Updates{1, 15, 19, 23});

    auto face = single(TriggerKind::Repeat, button::kCross);
    CHECK(face.updates_carrying(button::kCross, 24, kCmd) == Updates{1});
}

TEST_CASE("tapped matches when the button comes up after 1 to 6 updates down") {
    for (int down = 1; down <= 6; ++down) {
        auto pad = single(TriggerKind::Tapped, button::kCircle);
        CHECK(pad.updates_carrying(button::kCircle, down, kCmd) == Updates{down + 1});
    }
    auto pad = single(TriggerKind::Tapped, button::kCircle);
    CHECK(pad.updates_carrying(button::kCircle, 7, kCmd).empty());
}

TEST_CASE("long hold matches on a release after 1 to 3 updates, or on the 4th update down") {
    for (int down = 1; down <= 3; ++down) {
        auto pad = single(TriggerKind::LongHold, button::kCross);
        CHECK(pad.updates_carrying(button::kCross, down, kCmd) == Updates{down + 1});
    }
    auto pad = single(TriggerKind::LongHold, button::kCross);
    CHECK(pad.updates_carrying(button::kCross, 4, kCmd) == Updates{4});
    CHECK(pad.updates_carrying(button::kCross, 10, kCmd) == Updates{4});
}

TEST_CASE("history hold matches on the 5th, 6th and 7th update down") {
    auto pad = single(TriggerKind::HistoryHold, button::kTriangle);
    CHECK(pad.updates_carrying(button::kTriangle, 12, kCmd) == Updates{5, 6, 7});
    CHECK(pad.updates_carrying(button::kTriangle, 4, kCmd).empty());
}

TEST_CASE("combination held matches when the last button of both groups goes down") {
    auto pad = single(TriggerKind::CombinationHeld, button::kL1, button::kR1);
    CHECK(pad.step(button::kL1 | button::kR1) == kCmd);
    CHECK(pad.step(button::kL1 | button::kR1) == std::nullopt);
    CHECK(pad.step(0) == std::nullopt);

    CHECK(pad.step(button::kR1) == std::nullopt);
    CHECK(pad.step(button::kR1 | button::kL1) == kCmd); // either order
    CHECK(pad.step(button::kL1) == std::nullopt);
    CHECK(pad.step(button::kR1 | button::kL1) == kCmd); // down again after letting one go
}

TEST_CASE("combination press matches when the second group goes down while the first is held") {
    auto pad = single(TriggerKind::CombinationPress, button::kL2, button::kCross);
    CHECK(pad.step(button::kL2) == std::nullopt);
    CHECK(pad.step(button::kL2) == std::nullopt);
    CHECK(pad.step(button::kL2 | button::kCross) == kCmd);
    CHECK(pad.step(button::kL2 | button::kCross) == std::nullopt);
    CHECK(pad.step(0) == std::nullopt);

    CHECK(pad.step(button::kL2 | button::kCross) == kCmd); // together
    CHECK(pad.step(0) == std::nullopt);

    CHECK(pad.step(button::kCross) == std::nullopt); // the second group first: no command
    CHECK(pad.step(button::kCross | button::kL2) == std::nullopt);
}

TEST_CASE("a player carries one command per update, the lowest rank") {
    // The page's examples, with the bindings they use.
    Pad pad{.bindings = {{15, TriggerKind::Pressed, button::kSquare, 0},
                         {21, TriggerKind::Held, button::kSquare, 0},
                         {30, TriggerKind::Pressed, button::kCircle, 0},
                         {13, TriggerKind::Tapped, button::kCircle, 0},
                         {14, TriggerKind::HistoryHold, button::kCircle, 0},
                         {18, TriggerKind::Pressed, button::kCross, 0},
                         {22, TriggerKind::Held, button::kCross, 0},
                         {16, TriggerKind::LongHold, button::kCross, 0},
                         {7, TriggerKind::Pressed, button::kL1, 0},
                         {8, TriggerKind::Released, button::kL1, 0},
                         {3, TriggerKind::Pressed, button::kR1, 0},
                         {31, TriggerKind::CombinationHeld, button::kL1, button::kR1}},
            .pad = {}};
    using Carried = std::vector<std::optional<int>>;
    const auto none = std::nullopt;

    CHECK(pad.hold(button::kSquare, 3) == Carried{15, 21, 21, none});
    CHECK(pad.hold(button::kCircle, 3) == Carried{30, none, none, 13});
    CHECK(pad.hold(button::kCircle, 10) == Carried{30, none, none, none, 14, 14, 14, none, none, none, none});
    CHECK(pad.hold(button::kCross, 6) == Carried{18, 22, 22, 16, 22, 22, none});
    CHECK(pad.hold(button::kL1, 1) == Carried{7, 8});
    CHECK(pad.hold(button::kL1 | button::kR1, 1) == Carried{31, 8});
}

TEST_CASE("two kinds matching on one update: the lower rank is carried whatever the binding order") {
    Pad pad{.bindings = {{21, TriggerKind::Held, button::kSquare, 0}, {15, TriggerKind::Pressed, button::kSquare, 0}},
            .pad = {}};
    CHECK(pad.step(button::kSquare) == 15);
    CHECK(pad.step(button::kSquare) == 21);
}

TEST_CASE("two bindings of one rank: the first listed is carried") {
    Pad pad{.bindings = {{10, TriggerKind::Pressed, button::kTriangle, 0},
                         {18, TriggerKind::Pressed, button::kCross, 0}},
            .pad = {}};
    CHECK(pad.step(button::kCross | button::kTriangle) == 10);
}

TEST_CASE("the d-pad gives one direction only") {
    Pad pad{.bindings = {{38, TriggerKind::Pressed, button::kDpadUp, 0},
                         {40, TriggerKind::Pressed, button::kDpadRight, 0}},
            .pad = {}};
    pad.step(button::kDpadUp);
    CHECK(pad.pad.history.words[0] == button::kDpadUp);
    // A second direction joins: the one held longer stays.
    CHECK(pad.step(button::kDpadUp | button::kDpadRight) == std::nullopt);
    CHECK(pad.pad.history.words[0] == button::kDpadUp);
    // Let the first go: the other one reaches the game now, as a press.
    CHECK(pad.step(button::kDpadRight) == 40);
    CHECK(pad.pad.history.words[0] == button::kDpadRight);
    pad.step(0);

    // Two going down together: one of them, never both.
    const ButtonWord diagonal = button::kDpadDown | button::kDpadLeft | button::kCross;
    pad.step(diagonal);
    const ButtonWord dpad = pad.pad.history.words[0] & button::kDpad;
    CHECK((dpad == button::kDpadDown || dpad == button::kDpadLeft));
    CHECK((pad.pad.history.words[0] & button::kCross) != 0);
}

TEST_CASE("the history keeps the last 8 button words") {
    Pad pad;
    for (ButtonWord word = 1; word <= 9; ++word) {
        pad.step(word);
    }
    CHECK(pad.pad.history.words[0] == 9);
    CHECK(pad.pad.history.words[7] == 2);
}

TEST_CASE("a command switched off for the player is not carried") {
    Pad pad{.bindings = {{15, TriggerKind::Pressed, button::kSquare, 0}, {21, TriggerKind::Held, button::kSquare, 0}},
            .pad = {}};
    pad.pad.switched_off.set(15);
    CHECK(pad.step(button::kSquare) == 21);
    pad.pad.switched_off.reset(15);
    CHECK(pad.step(0) == std::nullopt);
    CHECK(pad.step(button::kSquare) == 15);
}
