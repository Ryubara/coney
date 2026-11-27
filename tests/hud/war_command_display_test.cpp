// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/war_command_display.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/font.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "hud/hud_audio.h"
#include "hud/hud_canvas.h"
#include "support/font_fixtures.h"
#include "support/hud_fixtures.h"
#include "support/recording_device.h"

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
        s.play = [this](std::string_view name) {
            played.emplace_back(name);
            return coney::hud::HudSoundHandle{0};
        };
        s.cueName = [](int cue) { return cue == coney::hud::kWarCommandCue ? std::string("move") : std::string(); };
        return s;
    }
};

// Where render() draws: the flat batch (each slot's backing then plate), part_page0 (the icons, then the name's box)
// and the name's font batch, over synthetic sheets.
struct Canvas {
    coney::graphics::SpriteBatch flat;
    coney::graphics::SpriteBatch parts;
    coney::graphics::Font font = coney::test::testFont();
    coney::graphics::SpriteBatch text;
    coney::hud::HudCanvas canvas;

    Canvas() : flat(sheet(), 64, 1.0F), parts(sheet(), 64, 1.0F), text(coney::test::testFontSheet(), 256, 1.0F) {
        canvas.flat = &flat;
        canvas.parts = &parts;
        canvas.text.fonts = [this](int /*slot*/) { return &font; };
        canvas.text.textBatch = [this](int /*slot*/) { return &text; };
    }

    // A sheet of 100 rectangles over a fake texture: enough for the icons (0x55-0x5b) and the name's box (0x4e).
    static coney::graphics::SpriteSheet sheet() {
        coney::graphics::SpriteSheet made;
        made.texture = std::make_shared<coney::test::FakeTexture>(256, 256);
        for (int i = 0; i < 100; ++i) {
            made.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 0.1F, 0.1F});
        }
        return made;
    }

    // Draws `display` at `nowMs` with every command enabled but those `enabled` refuses.
    void draw(
        const WarCommandDisplay& display, std::uint64_t nowMs,
        std::function<bool(int)> enabled = [](int) { return true; }) {
        flat.clear();
        parts.clear();
        text.clear();
        WarCommandDisplay::Look look;
        look.name = [](std::size_t /*entry*/) { return std::string("GO"); };
        look.enabled = std::move(enabled);
        display.render(canvas, look, nowMs);
    }
};

// Whether two colours agree but for their alpha.
bool sameColour(coney::graphics::Rgba a, coney::graphics::Rgba b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

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

TEST_CASE("an open on a display that is not allowed leaves it as it is, even for a human who is not a war chief",
          "[hud][war-commands]") {
    // Expected: hud.md#warrior-command-menu, confirmed (code)
    WarCommandDisplay display;
    WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    REQUIRE(display.shown());
    chief.allowed = false;
    chief.warChief = false;
    display.open(chief, 33);
    CHECK(display.shown());
}

TEST_CASE("a slot is lit only once the stick has passed the dead zone or an order was given", "[hud][war-commands]") {
    // Expected: hud.md#war-command-colours, confirmed (code)
    const coney::graphics::Rgba gold{255, 183, 0, 255};
    const coney::graphics::Rgba grey{35, 35, 35, 255};
    WarCommandDisplay display;
    Canvas canvas;
    const WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    // Opened with the stick at rest: slot 0 is highlighted but not lit, its plate the others' grey.
    display.update(raw(0.0F), raw(0.0F), chief, 33, coney::test::kSilent);
    canvas.draw(display, 33);
    REQUIRE(canvas.flat.sprites().size() == 12);
    CHECK(sameColour(canvas.flat.sprites()[1].colour, grey));
    // The stick past the dead zone, still on slot 0, lights it.
    display.update(raw(0.0F), raw(-1.0F), chief, 66, coney::test::kSilent);
    REQUIRE(display.highlight() == 0);
    canvas.draw(display, 66);
    REQUIRE(canvas.flat.sprites().size() == 12);
    CHECK(sameColour(canvas.flat.sprites()[1].colour, gold));
    // An order lights it too, on a fresh display that never saw the stick.
    WarCommandDisplay ordered;
    ordered.open(chief, 0);
    REQUIRE(ordered.issue() == 0);
    canvas.draw(ordered, 0);
    REQUIRE(canvas.flat.sprites().size() == 12);
    CHECK(sameColour(canvas.flat.sprites()[1].colour, gold));
}

TEST_CASE("the lit slot's backing is the grey of an unlit plate", "[hud][war-commands]") {
    // Expected: hud.md#war-command-colours, confirmed (code)
    WarCommandDisplay display;
    Canvas canvas;
    const WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    display.update(raw(0.0F), raw(-1.0F), chief, 33, coney::test::kSilent);
    canvas.draw(display, 33);
    REQUIRE(canvas.flat.sprites().size() == 12);
    // Slot 0 lit: backing (35, 35, 35), plate gold; slot 1: backing black, plate (35, 35, 35).
    CHECK(sameColour(canvas.flat.sprites()[0].colour, coney::graphics::Rgba{35, 35, 35, 255}));
    CHECK(sameColour(canvas.flat.sprites()[2].colour, coney::graphics::Rgba{0, 0, 0, 255}));
    CHECK(sameColour(canvas.flat.sprites()[3].colour, coney::graphics::Rgba{35, 35, 35, 255}));
}

TEST_CASE("after an order the chosen icon fades over 1500 ms and its backing and plate over 500 ms",
          "[hud][war-commands]") {
    // Expected: hud.md#warrior-command-menu, confirmed (code)
    WarCommandDisplay display;
    Canvas canvas;
    const WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    REQUIRE(display.issue() == 0);
    // Twelve updates 33 ms apart: the fade starts on the 10th (330 ms); the 12th leaves the chosen slot shown.
    for (int n = 1; n <= 12; ++n) {
        display.update(raw(0.0F), raw(0.0F), chief, static_cast<std::uint64_t>(n) * 33, coney::test::kSilent);
    }
    // 250 ms into the fade: the backing and plate are half gone, the icon a sixth (212 or 213 of 255).
    canvas.draw(display, 580);
    REQUIRE(canvas.flat.sprites().size() == 12);
    CHECK(canvas.flat.sprites()[0].colour.a == 128);
    CHECK(canvas.flat.sprites()[1].colour.a == 128);
    REQUIRE(canvas.parts.sprites().size() >= 6);
    CHECK(canvas.parts.sprites()[0].colour.a >= 212);
    CHECK(canvas.parts.sprites()[0].colour.a <= 213);
    // The other slots' sprites fade over 500 ms too.
    CHECK(canvas.flat.sprites()[3].colour.a == 128);
    CHECK(canvas.parts.sprites()[1].colour.a == 128);
}

TEST_CASE("the chosen slot blinks from the order on: hidden for two updates, then one update in four",
          "[hud][war-commands]") {
    // Expected: hud.md#warrior-command-menu, confirmed (code)
    WarCommandDisplay display;
    Canvas canvas;
    const WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    REQUIRE(display.issue() == 0);
    std::vector<bool> hidden;
    for (int n = 1; n <= 14; ++n) {
        display.update(raw(0.0F), raw(0.0F), chief, static_cast<std::uint64_t>(n) * 10, coney::test::kSilent);
        canvas.draw(display, static_cast<std::uint64_t>(n) * 10);
        hidden.push_back(canvas.flat.sprites().size() == 10);
    }
    const std::vector<bool> expected{true,  true,  false, false, false, true,  false,
                                     false, false, true,  false, false, false, true};
    CHECK(hidden == expected);
}

TEST_CASE("nothing is drawn after an order of a disabled command", "[hud][war-commands]") {
    // Expected: hud.md#war-command-colours, confirmed (code)
    WarCommandDisplay display;
    Canvas canvas;
    const WarCommandDisplay::Chief chief;
    const auto followOff = [](int command) { return command != 0; };
    display.open(chief, 0);
    // While picking, the disabled command's slot is drawn.
    canvas.draw(display, 0, followOff);
    CHECK(canvas.flat.sprites().size() == 12);
    REQUIRE(display.issue() == 0);
    canvas.draw(display, 0, followOff);
    CHECK(canvas.flat.sprites().empty());
    CHECK(canvas.parts.sprites().empty());
    CHECK(canvas.text.sprites().empty());
}

TEST_CASE("a display that is not allowed draws nothing", "[hud][war-commands]") {
    // Expected: hud.md#war-command-colours, confirmed (code)
    WarCommandDisplay display;
    Canvas canvas;
    WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    chief.allowed = false;
    display.update(raw(0.0F), raw(0.0F), chief, 33, coney::test::kSilent);
    REQUIRE(display.shown());
    canvas.draw(display, 33);
    CHECK(canvas.flat.sprites().empty());
}

TEST_CASE("the name is grey while picking, dark for a disabled command and gold after the order",
          "[hud][war-commands]") {
    // Expected: hud.md#war-command-colours, confirmed (code)
    WarCommandDisplay display;
    Canvas canvas;
    const WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    canvas.draw(display, 0);
    REQUIRE_FALSE(canvas.text.sprites().empty());
    CHECK(sameColour(canvas.text.sprites()[0].colour, coney::graphics::Rgba{110, 110, 110, 255}));
    canvas.draw(display, 0, [](int command) { return command != 0; });
    REQUIRE_FALSE(canvas.text.sprites().empty());
    CHECK(sameColour(canvas.text.sprites()[0].colour, coney::graphics::Rgba{30, 30, 30, 255}));
    REQUIRE(display.issue() == 0);
    canvas.draw(display, 0);
    REQUIRE_FALSE(canvas.text.sprites().empty());
    CHECK(sameColour(canvas.text.sprites()[0].colour, coney::graphics::Rgba{255, 183, 0, 255}));
}
