// SPDX-License-Identifier: GPL-3.0-or-later
// The Rumble match's screens (docs/research/rumble.md#intro, docs/research/rumble.md#result-screen): the intro's
// names, prompt and countdown, and the result screen's timing, grids and choices, with what mode 0x14 makes of them.
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "gamemodes/rumble_result_mode.h"
#include "gui/global_strings.h"
#include "gui/pause_menu/pause_menu.h"
#include "gui/rumble_mode_gui/rumble_intro.h"
#include "gui/rumble_mode_gui/rumble_result_menu.h"

using coney::Pad;
using coney::PadSample;
using coney::gui::GlobalStrings;
using coney::gui::GuiFrame;
using coney::gui::PauseOutcome;
using coney::gui::RumbleIntro;
using coney::gui::RumbleIntroPhase;
using coney::gui::RumbleIntroSounds;
using coney::gui::RumbleResultChoice;
using coney::gui::RumbleResultMenu;

namespace {

// Every string the screens ask for, as "#<hex id>".
GlobalStrings syntheticStrings() {
    GlobalStrings strings;
    for (std::uint32_t id = 0; id < 0x100; ++id) {
        strings.set(id, std::format("#{:x}", id));
    }
    return strings;
}

// A connected sample holding `buttons`, the sticks at rest.
PadSample sample(std::uint16_t buttons) {
    PadSample s;
    s.connected = true;
    s.buttons = buttons;
    s.sticks = {coney::pad::kStickCentre, coney::pad::kStickCentre, coney::pad::kStickCentre, coney::pad::kStickCentre};
    return s;
}

// A screen with a pad, run one 1/30 s frame at a time from game time 10 s.
template <typename Screen> struct Driver {
    Screen& screen;
    Pad pad;
    std::uint64_t nowMs = 10'000;
    std::uint64_t frame = 0;

    // Drives `driven` from game time 10 s.
    explicit Driver(Screen& driven) : screen(driven) {}
    // One frame holding `buttons`.
    void step(std::uint16_t buttons = 0) {
        pad.update(sample(buttons));
        ++frame;
        nowMs = 10'000 + (frame * 100 / 3);
        screen.update(GuiFrame{nowMs, &pad});
    }
    // Idle frames up to game time `untilMs`.
    void idleUntil(std::uint64_t untilMs) {
        while (nowMs < untilMs) {
            step();
        }
    }
    // A press of `buttons` and its release, then a gap.
    void tap(std::uint16_t buttons) {
        step(buttons);
        for (int i = 0; i < 5; ++i) {
            step();
        }
    }
};

// A result screen opened at game time 10 s with the synthetic strings, its cues kept.
struct Result {
    GlobalStrings strings = syntheticStrings();
    RumbleResultMenu menu;
    std::vector<int> cues;
    Driver<RumbleResultMenu> driver{menu};

    explicit Result(bool fromFrontEnd, std::string_view winner = "ORPHANS") {
        menu.setStrings(&strings);
        menu.setSoundSink([this](int cue) { cues.push_back(cue); });
        menu.open(winner, "WIN!", fromFrontEnd, driver.nowMs);
    }
    // Waits until the screen takes input.
    void waitForChoices() {
        driver.idleUntil(10'000 + RumbleResultMenu::kChoicesDelayMs + 1000 / 15 + RumbleResultMenu::kChoicesFadeMs);
    }
    // The selected item's id.
    [[nodiscard]] int selected() const { return menu.grid().code(menu.grid().selected()); }
};

// An intro over the synthetic strings whose gang lines cannot play and whose other lines last 1 s.
struct Intro {
    GlobalStrings strings = syntheticStrings();
    RumbleIntro intro;
    std::vector<std::string> sounds;
    std::vector<std::string> done;
    Driver<RumbleIntro> driver{intro};

    Intro() {
        intro.setStrings(&strings);
        intro.setSounds(RumbleIntroSounds{.playVoice = [](std::string_view name) -> std::optional<std::uint64_t> {
                                              if (name.starts_with("dj_gang_")) {
                                                  return std::nullopt;
                                              }
                                              return 1000;
                                          },
                                          .playSound = [this](std::string_view name) { sounds.emplace_back(name); }});
        intro.setDoneSink([this](std::string_view onDone) { done.emplace_back(onDone); });
    }
};

} // namespace

TEST_CASE("rumble result: the winner and reason show at once, the choices after 6.5 s, input after their fade",
          "[rumble]") {
    Result result(true);
    CHECK(result.menu.winner().text() == "ORPHANS");
    CHECK(result.menu.reason().text() == "WIN!");
    CHECK(result.menu.grid().code(0) == RumbleResultMenu::kReplayId);
    CHECK(result.menu.grid().code(1) == RumbleResultMenu::kMoreId);
    result.driver.idleUntil(10'000 + RumbleResultMenu::kChoicesDelayMs - 100);
    CHECK_FALSE(result.menu.choicesShown());
    // Accept before the choices are taken does nothing.
    result.driver.tap(coney::pad::kCross);
    CHECK_FALSE(result.menu.choice());
    result.driver.idleUntil(10'000 + RumbleResultMenu::kChoicesDelayMs + 100);
    CHECK(result.menu.choicesShown());
    CHECK_FALSE(result.menu.takesInput());
    result.waitForChoices();
    CHECK(result.menu.takesInput());

    Result unnamed(true, "");
    CHECK(unnamed.menu.winner().text() == "#e0");
}

TEST_CASE("rumble result: Replay ends the screen; back is ignored", "[rumble]") {
    Result result(true);
    result.waitForChoices();
    result.driver.tap(coney::pad::kTriangle);
    CHECK_FALSE(result.menu.choice());
    REQUIRE(result.selected() == RumbleResultMenu::kReplayId);
    result.driver.tap(coney::pad::kCross);
    CHECK(result.menu.choice() == RumbleResultChoice::Replay);
    CHECK(result.cues.back() == RumbleResultMenu::kAcceptCue);
}

TEST_CASE("rumble result: more shows Rumble menu : Quit, Quit's text by where the match started", "[rumble]") {
    Result result(true);
    result.waitForChoices();
    result.driver.tap(coney::pad::kRight);
    REQUIRE(result.selected() == RumbleResultMenu::kMoreId);
    result.driver.tap(coney::pad::kCross);
    CHECK_FALSE(result.menu.choice());
    REQUIRE(result.menu.secondGrid());
    CHECK(result.menu.grid().code(0) == RumbleResultMenu::kRumbleMenuId);
    CHECK(result.menu.grid().code(1) == RumbleResultMenu::kQuitId);
    result.driver.tap(coney::pad::kRight);
    result.driver.tap(coney::pad::kCross);
    CHECK(result.menu.choice() == RumbleResultChoice::Quit);

    Result inGame(false);
    inGame.waitForChoices();
    inGame.driver.tap(coney::pad::kRight);
    inGame.driver.tap(coney::pad::kCross);
    REQUIRE(inGame.menu.secondGrid());
    inGame.driver.tap(coney::pad::kCross);
    CHECK(inGame.menu.choice() == RumbleResultChoice::RumbleMenu);
}

TEST_CASE("rumble result: each choice maps onto a pause outcome by where the match started", "[rumble]") {
    using coney::rumbleResultOutcome;
    CHECK(rumbleResultOutcome(RumbleResultChoice::Replay, true) == PauseOutcome::RestartLevel);
    CHECK(rumbleResultOutcome(RumbleResultChoice::Replay, false) == PauseOutcome::RestartLevel);
    CHECK(rumbleResultOutcome(RumbleResultChoice::RumbleMenu, true) == PauseOutcome::QuitToRumbleQuick);
    CHECK(rumbleResultOutcome(RumbleResultChoice::RumbleMenu, false) == PauseOutcome::QuitToRumbleHangout);
    CHECK(rumbleResultOutcome(RumbleResultChoice::Quit, true) == PauseOutcome::QuitToMainMenu);
    CHECK(rumbleResultOutcome(RumbleResultChoice::Quit, false) == PauseOutcome::QuitToHangout);
}

TEST_CASE("rumble intro: the names one at a time, general lines when the gang's cannot play, then the prompt",
          "[rumble]") {
    Intro intro;
    const std::vector<std::string> names{"BASEBALL FURIES", "", "ORPHANS"};
    intro.intro.open("RumbleGo", names, {3, 7}, intro.driver.nowMs);
    CHECK(intro.intro.phase() == RumbleIntroPhase::Names);
    CHECK(intro.intro.names() == std::vector<std::string>{"BASEBALL FURIES", "ORPHANS"});
    CHECK(intro.intro.shown() == 1);
    // Without random numbers the first general line is 01, and the second name takes the next one.
    intro.driver.idleUntil(10'000 + 3100);
    CHECK(intro.intro.shown() == 2);
    CHECK(intro.intro.voices() == std::vector<std::string>{"dj_genintro_01", "dj_vs_01", "dj_genintro_02"});
    CHECK(intro.sounds == std::vector<std::string>{"rumblesynth_01", "rumblesynth_02"});
    // Each line lasts 1 s: the prompt starts 3 s in, and takes the pad once nearly opaque.
    CHECK(intro.intro.phase() == RumbleIntroPhase::Prompt);
    CHECK_FALSE(intro.intro.holdsPad());
    intro.driver.idleUntil(10'000 + 4100);
    CHECK(intro.intro.holdsPad());
}

TEST_CASE("rumble intro: accept plays the ready line, then 3, 2, 1 and the last word, then onDone", "[rumble]") {
    Intro intro;
    intro.intro.open("RumbleGo", {}, {0, 1}, intro.driver.nowMs);
    REQUIRE(intro.intro.phase() == RumbleIntroPhase::Prompt);
    intro.driver.idleUntil(10'000 + 1100);
    REQUIRE(intro.intro.holdsPad());
    intro.driver.tap(coney::pad::kCross);
    CHECK(intro.intro.phase() == RumbleIntroPhase::Ready);
    CHECK(intro.intro.voices().back() == "dj_ready_01");

    // The ready line lasts 1 s; then the countdown, a word a second.
    for (int k = 0; k < 60 && intro.intro.phase() == RumbleIntroPhase::Ready; ++k) {
        intro.driver.step();
    }
    REQUIRE(intro.intro.phase() == RumbleIntroPhase::Countdown);
    const std::uint64_t start = intro.driver.nowMs;
    CHECK(intro.intro.countdownWord(start) == "3");
    CHECK(intro.intro.countdownWord(start + 1000) == "2");
    CHECK(intro.intro.countdownWord(start + 2000) == "1");
    CHECK(intro.intro.countdownWord(start + 3000) == "#3f");
    intro.driver.idleUntil(start + 3100);
    CHECK(intro.intro.voices().back() == "dj_start_01");
    CHECK(intro.done.empty());
    intro.driver.idleUntil(start + RumbleIntro::kCountdownMs + 100);
    CHECK_FALSE(intro.intro.isOpen());
    CHECK(intro.done == std::vector<std::string>{"RumbleGo"});
}

TEST_CASE("rumble intro: closing it ends it without onDone", "[rumble]") {
    Intro intro;
    intro.intro.open("RumbleGo", std::vector<std::string>{"A", "B"}, {0, 1}, intro.driver.nowMs);
    intro.intro.close();
    CHECK_FALSE(intro.intro.isOpen());
    intro.driver.idleUntil(10'000 + 10'000);
    CHECK(intro.done.empty());
}
