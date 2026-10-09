// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/war_command_display.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hud/hud_audio.h"

namespace {

using coney::hud::WarCommandDisplay;
using coney::hud::warCommandOfSlot;
using coney::hud::warCommandSlotAt;
using coney::hud::warCommandStickAngle;

// A stick value in [-1, 1] as its raw byte (127.5 + 127.5 v, y down), as an input script's percentages give it.
std::uint8_t raw(float value) { return static_cast<std::uint8_t>(127.5F + (127.5F * value)); }

// The sound output that records the cue names it is asked to play.
struct Sounds {
    std::vector<std::string> played;
    coney::hud::HudSound sound() {
        coney::hud::HudSound s;
        s.play = [this](std::string_view name) { played.emplace_back(name); };
        s.cueName = [](int cue) { return cue == coney::hud::kWarCommandCue ? std::string("move") : std::string(); };
        return s;
    }
};

} // namespace

TEST_CASE("the command menu's slots map to the Warrior commands clockwise from up", "[hud][war-commands]") {
    CHECK(warCommandOfSlot(0) == 0); // follow
    CHECK(warCommandOfSlot(1) == 2); // defend
    CHECK(warCommandOfSlot(2) == 4); // scatter
    CHECK(warCommandOfSlot(3) == 3); // hold
    CHECK(warCommandOfSlot(4) == 5); // wreck
    CHECK(warCommandOfSlot(5) == 1); // attack
    CHECK_FALSE(warCommandOfSlot(6).has_value());
}

TEST_CASE("the menu reads the right stick only past its dead zone, with a one-axis angle", "[hud][war-commands]") {
    // At rest and at 80 % nothing is read; 90 % up is.
    CHECK_FALSE(warCommandStickAngle(raw(0.0F), raw(0.0F)).has_value());
    CHECK_FALSE(warCommandStickAngle(raw(0.0F), raw(-0.8F)).has_value());
    REQUIRE(warCommandStickAngle(raw(0.0F), raw(-0.9F)).has_value());
    // value_or(-1): a missing angle fails the check instead of being read unchecked.
    CHECK(warCommandStickAngle(128, 0).value_or(-1.0F) == Catch::Approx(0.0F).margin(0.5F));
    // Right (y on 128), down (x on 127) and left (y on 128) read 90°, 180° and 270° (an offset of 128 counts as 90°).
    CHECK(warCommandStickAngle(255, 128).value_or(-1.0F) == Catch::Approx(90.0F).margin(0.5F));
    CHECK(warCommandStickAngle(127, 255).value_or(-1.0F) == Catch::Approx(180.0F).margin(0.5F));
    CHECK(warCommandStickAngle(0, 128).value_or(-1.0F) == Catch::Approx(270.0F).margin(0.5F));
    // Only one axis is read: full right with the y byte at 127 is in the up-right quadrant and reads asin(127/128).
    CHECK(warCommandStickAngle(255, 127).value_or(-1.0F) == Catch::Approx(82.8F).margin(0.5F));
    // Straight up from the 127 centre lands in the up-left quadrant at 360°, read as 0°.
    CHECK(warCommandStickAngle(127, 0).value_or(-1.0F) == Catch::Approx(0.0F).margin(0.5F));
    // A 45° push of 0.9 up-right reads about 39°: asin of the x offset only.
    CHECK(warCommandStickAngle(raw(0.636F), raw(-0.636F)).value_or(-1.0F) == Catch::Approx(39.0F).margin(1.0F));
}

TEST_CASE("the highlighted sector reaches 11.25 degrees into its neighbours", "[hud][war-commands]") {
    CHECK(warCommandSlotAt(10.0F, 0) == 0);
    CHECK(warCommandSlotAt(30.0F, 0) == 0); // within 22.5 + 11.25 of up
    CHECK(warCommandSlotAt(35.0F, 0) == 1); // past it: up-right
    CHECK(warCommandSlotAt(30.0F, 1) == 1);
    CHECK(warCommandSlotAt(15.0F, 1) == 1); // up-right holds until 22.5 - 11.25
    CHECK(warCommandSlotAt(10.0F, 1) == 0);
    CHECK(warCommandSlotAt(315.0F, 0) == 5); // up-left: attack
    // Straight right selects nothing new.
    CHECK(warCommandSlotAt(90.0F, 3) == 3);
}

TEST_CASE("R2 opens the menu, the stick moves the highlight and the release gives its command once",
          "[hud][war-commands]") {
    WarCommandDisplay display;
    Sounds sounds;
    const coney::hud::HudSound sound = sounds.sound();
    const WarCommandDisplay::Chief chief;
    // Not open: a release gives nothing.
    CHECK_FALSE(display.issue().has_value());
    display.open(chief, 0);
    CHECK(display.shown());
    CHECK_FALSE(display.cameraStickOn());
    // The stick up-left at full deflection highlights slot 5 (attack), with the cue.
    display.update(raw(-0.71F), raw(-0.71F), chief, 33, sound);
    CHECK(display.highlight() == 5);
    CHECK(sounds.played.size() == 1);
    // Held there, nothing more is played.
    display.update(raw(-0.71F), raw(-0.71F), chief, 66, sound);
    CHECK(sounds.played.size() == 1);
    // The release gives attack (1), once.
    CHECK(display.issue() == 1);
    CHECK_FALSE(display.issue().has_value());
    // The camera's stick comes back after the 10-update hold.
    for (int i = 0; i < coney::hud::kWarCommandHoldUpdates; ++i) {
        display.update(raw(0.0F), raw(0.0F), chief, 100 + (i * 33), sound);
        CHECK_FALSE(display.cameraStickOn());
    }
    display.update(raw(0.0F), raw(0.0F), chief, 500, sound);
    CHECK(display.cameraStickOn());
    // Opened again it starts on the last highlighted slot, and a release without the stick re-issues it.
    display.open(chief, 600);
    CHECK(display.highlight() == 5);
    CHECK(display.issue() == 1);
}

TEST_CASE("a lock that arrives while the menu is up ends it with no command", "[hud][war-commands]") {
    WarCommandDisplay display;
    Sounds sounds;
    const coney::hud::HudSound sound = sounds.sound();
    WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    chief.menuLocked = true;
    display.update(raw(0.0F), raw(-1.0F), chief, 33, sound);
    CHECK(display.issued());
    CHECK_FALSE(display.issue().has_value());
}

TEST_CASE("the menu closes once the name's display time has run out after an order", "[hud][war-commands]") {
    WarCommandDisplay display;
    Sounds sounds;
    const coney::hud::HudSound sound = sounds.sound();
    WarCommandDisplay::Chief chief;
    chief.nameDisplayMs = 2500;
    display.open(chief, 0);
    REQUIRE(display.issue() == 0);
    std::uint64_t now = 0;
    for (int i = 0; i < 60 && display.shown(); ++i) {
        now += 33;
        display.update(raw(0.0F), raw(0.0F), chief, now, sound);
    }
    CHECK(display.shown());
    for (int i = 0; i < 60 && display.shown(); ++i) {
        now += 33;
        display.update(raw(0.0F), raw(0.0F), chief, now, sound);
    }
    CHECK_FALSE(display.shown());
    CHECK(now >= 2500);
}

TEST_CASE("a human who is not a war chief gets no menu", "[hud][war-commands]") {
    WarCommandDisplay display;
    WarCommandDisplay::Chief chief;
    chief.warChief = false;
    display.open(chief, 0);
    CHECK_FALSE(display.shown());
    chief.warChief = true;
    chief.allowed = false;
    display.open(chief, 0);
    CHECK_FALSE(display.shown());
}
