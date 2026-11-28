// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/war_command_display.h"

#include <algorithm>
#include <array>
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

// Where render() draws: big_font (resource instance 6, font slot 6: each slot's backing then plate), part_page0 (the
// icons) and the name's font batch, over synthetic sheets; the flat batch must stay empty.
struct Canvas {
    coney::graphics::SpriteBatch flat;
    coney::graphics::SpriteBatch slots;
    coney::graphics::SpriteBatch parts;
    coney::graphics::Font font = coney::test::testFont();
    coney::graphics::SpriteBatch text;
    coney::hud::HudCanvas canvas;

    Canvas()
        : flat(coney::graphics::SpriteSheet{}, 64, 1.0F), slots(bigFontSheet(), 64, 1.0F), parts(sheet(), 64, 1.0F),
          text(coney::test::testFontSheet(), 256, 1.0F) {
        canvas.flat = &flat;
        canvas.parts = &parts;
        canvas.text.fonts = [this](int /*slot*/) { return &font; };
        canvas.text.textBatch = [this](int slot) { return slot == 6 ? &slots : &text; };
    }

    // A sheet of 100 rectangles over a fake texture: enough for the icons (0x55-0x5b).
    static coney::graphics::SpriteSheet sheet() {
        coney::graphics::SpriteSheet made;
        made.texture = std::make_shared<coney::test::FakeTexture>(256, 256);
        for (int i = 0; i < 100; ++i) {
            made.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 0.1F, 0.1F});
        }
        return made;
    }

    // A big_font stand-in: 300 rectangles, rectangle 256 (the slots' disc) 64 x 32 texels, the rest 25 x 25.
    static coney::graphics::SpriteSheet bigFontSheet() {
        coney::graphics::SpriteSheet made;
        made.texture = std::make_shared<coney::test::FakeTexture>(256, 256);
        for (int i = 0; i < 300; ++i) {
            made.page.rects.push_back(i == 256 ? kDisc : coney::graphics::UvRect{0.0F, 0.0F, 0.1F, 0.1F});
        }
        return made;
    }
    static constexpr coney::graphics::UvRect kDisc{0.5F, 0.5F, 0.75F, 0.625F};

    // Draws `display` at `nowMs` with every command enabled but those `enabled` refuses.
    void draw(
        const WarCommandDisplay& display, std::uint64_t nowMs,
        std::function<bool(int)> enabled = [](int) { return true; }) {
        flat.clear();
        slots.clear();
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
    // Expected: hud.md#warrior-command-menu, confirmed (code)
    // At rest and at 80 % nothing is read; 90 % up is.
    CHECK_FALSE(warCommandStickAngle(raw(0.0F), raw(0.0F)).has_value());
    CHECK_FALSE(warCommandStickAngle(raw(0.0F), raw(-0.8F)).has_value());
    REQUIRE(warCommandStickAngle(raw(0.0F), raw(-0.9F)).has_value());
    // value_or(-1): a missing angle fails the check instead of being read unchecked. Right (y on 129), down (x on
    // 127) and left (y on 128) read 90°, 180° and 270° (an offset of 128 counts as 90°).
    CHECK(warCommandStickAngle(255, 129).value_or(-1.0F) == Catch::Approx(90.0F).margin(0.5F));
    CHECK(warCommandStickAngle(127, 255).value_or(-1.0F) == Catch::Approx(180.0F).margin(0.5F));
    CHECK(warCommandStickAngle(0, 128).value_or(-1.0F) == Catch::Approx(270.0F).margin(0.5F));
    // Only one axis is read: full right with the y byte at 127 is in the up-right quadrant and reads asin(127/128).
    CHECK(warCommandStickAngle(255, 127).value_or(-1.0F) == Catch::Approx(82.8F).margin(0.5F));
    // Straight up from the 127 centre lands in the up-left quadrant at 360°, read as 0°.
    CHECK(warCommandStickAngle(127, 0).value_or(-1.0F) == Catch::Approx(0.0F).margin(0.5F));
    // A 45° push of 0.9 up-right reads about 39°: asin of the x offset only.
    CHECK(warCommandStickAngle(raw(0.636F), raw(-0.636F)).value_or(-1.0F) == Catch::Approx(39.0F).margin(1.0F));
}

// docs/research/hud.md#warrior-command-menu: a byte of exactly 128 counts as up for the quadrant and on both sides for
// the axis read (WarCommandDisplay_ReadStick, 0x001a7040), which is why a stick exactly up at x 128 leaves the
// highlight where it was (seen at runtime).
TEST_CASE("a stick byte of exactly 128 counts as up for the quadrant and on both sides for the axis read",
          "[hud][war-commands]") {
    // Expected: hud.md#warrior-command-menu, confirmed (code)
    // Exactly up at x 128: the up-right quadrant, read on the y axis: 90°, between slots, so nothing new is picked.
    CHECK(warCommandStickAngle(128, 0).value_or(-1.0F) == Catch::Approx(90.0F).margin(0.5F));
    CHECK(warCommandSlotAt(warCommandStickAngle(128, 0).value_or(-1.0F), 3) == 3);
    // Full right with y on 128: the up-right quadrant read on the y axis, an offset of 0: 0°, the up slot.
    CHECK(warCommandStickAngle(255, 128).value_or(-1.0F) == Catch::Approx(0.0F).margin(0.5F));
    CHECK(warCommandSlotAt(warCommandStickAngle(255, 128).value_or(-1.0F), 2) == 0);
    // Exactly down at x 128: the down-right quadrant read on the y axis, about 173°, the down slot.
    CHECK(warCommandStickAngle(128, 255).value_or(-1.0F) == Catch::Approx(172.9F).margin(0.5F));
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
    // Expected: hud.md#warrior-command-menu, confirmed (code)
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
    REQUIRE(canvas.slots.sprites().size() == 12);
    CHECK(sameColour(canvas.slots.sprites()[1].colour, grey));
    // The stick past the dead zone, still on slot 0, lights it.
    display.update(raw(0.0F), raw(-1.0F), chief, 66, coney::test::kSilent);
    REQUIRE(display.highlight() == 0);
    canvas.draw(display, 66);
    REQUIRE(canvas.slots.sprites().size() == 12);
    CHECK(sameColour(canvas.slots.sprites()[1].colour, gold));
    // An order lights it too, on a fresh display that never saw the stick.
    WarCommandDisplay ordered;
    ordered.open(chief, 0);
    REQUIRE(ordered.issue() == 0);
    canvas.draw(ordered, 0);
    REQUIRE(canvas.slots.sprites().size() == 12);
    CHECK(sameColour(canvas.slots.sprites()[1].colour, gold));
}

TEST_CASE("the lit slot's backing is the grey of an unlit plate", "[hud][war-commands]") {
    // Expected: hud.md#war-command-colours, confirmed (code)
    WarCommandDisplay display;
    Canvas canvas;
    const WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    display.update(raw(0.0F), raw(-1.0F), chief, 33, coney::test::kSilent);
    canvas.draw(display, 33);
    REQUIRE(canvas.slots.sprites().size() == 12);
    // Slot 0 lit: backing (35, 35, 35), plate gold; slot 1: backing black, plate (35, 35, 35).
    CHECK(sameColour(canvas.slots.sprites()[0].colour, coney::graphics::Rgba{35, 35, 35, 255}));
    CHECK(sameColour(canvas.slots.sprites()[2].colour, coney::graphics::Rgba{0, 0, 0, 255}));
    CHECK(sameColour(canvas.slots.sprites()[3].colour, coney::graphics::Rgba{35, 35, 35, 255}));
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
    REQUIRE(canvas.slots.sprites().size() == 12);
    CHECK(canvas.slots.sprites()[0].colour.a == 128);
    CHECK(canvas.slots.sprites()[1].colour.a == 128);
    REQUIRE(canvas.parts.sprites().size() >= 6);
    CHECK(canvas.parts.sprites()[0].colour.a >= 212);
    CHECK(canvas.parts.sprites()[0].colour.a <= 213);
    // The other slots' sprites fade over 500 ms too.
    CHECK(canvas.slots.sprites()[3].colour.a == 128);
    CHECK(canvas.parts.sprites()[1].colour.a == 128);
}

TEST_CASE("the name's display time counts from the last update before the order", "[hud][war-commands]") {
    // Expected: hud.md#warrior-command-menu, confirmed (code): WarCommandDisplay_Update (0x001a7e48) sets the name's
    // text on every update while picking, and MarkupText_SetText (0x001bb250) clears its end time; seen in PCSX2 at
    // level80 checkpoint 2 (the gold name still shown 1.6 s after the order, 3.7 s after the highlight was set).
    WarCommandDisplay display;
    Sounds sounds;
    const coney::hud::HudSound sound = sounds.sound();
    WarCommandDisplay::Chief chief;
    chief.nameDisplayMs = 2500;
    display.open(chief, 0);
    // Two seconds of picking with the highlight left where it was, then the order.
    std::uint64_t now = 0;
    while (now < 2000) {
        now += 33;
        display.update(raw(0.0F), raw(0.0F), chief, now, sound);
    }
    REQUIRE(display.issue() == 0);
    const std::uint64_t ordered = now;
    while (now < ordered + 2400) {
        now += 33;
        display.update(raw(0.0F), raw(0.0F), chief, now, sound);
    }
    CHECK(display.shown());
    while (now < ordered + 2600) {
        now += 33;
        display.update(raw(0.0F), raw(0.0F), chief, now, sound);
    }
    CHECK_FALSE(display.shown());
}

TEST_CASE("the name keeps its full alpha while the slots fade after an order", "[hud][war-commands]") {
    // Expected: hud.md#warrior-command-menu, confirmed (code): WarCommandDisplay_Render (0x001a8590) sets the text's
    // +0x18c to 255 on every draw; seen in PCSX2 (the gold name whole after the slots had gone).
    WarCommandDisplay display;
    Canvas canvas;
    const WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    REQUIRE(display.issue() == 0);
    for (int n = 1; n <= 12; ++n) {
        display.update(raw(0.0F), raw(0.0F), chief, static_cast<std::uint64_t>(n) * 33, coney::test::kSilent);
    }
    // 250 ms into the fade the slots are half gone; the name is not.
    canvas.draw(display, 580);
    REQUIRE(canvas.slots.sprites().size() == 12);
    CHECK(canvas.slots.sprites()[3].colour.a == 128);
    REQUIRE_FALSE(canvas.text.sprites().empty());
    for (const auto& glyph : canvas.text.sprites()) {
        CHECK(glyph.colour.a == 255);
    }
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
        hidden.push_back(canvas.slots.sprites().size() == 10);
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
    CHECK(canvas.slots.sprites().size() == 12);
    REQUIRE(display.issue() == 0);
    canvas.draw(display, 0, followOff);
    CHECK(canvas.slots.sprites().empty());
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
    CHECK(canvas.slots.sprites().empty());
}

TEST_CASE("the name is centred under the menu with no box behind it", "[hud][war-commands]") {
    // Expected: hud.md#war-command-colours, confirmed (code): WarCommandDisplay_Render (0x001a8590) draws the slots and
    // the name's text (+0x50) only, never the box widget (+0x240); the command texts open with <CENTER>, which
    // MessageHUD_Layout (0x001b9600) turns into a start half the text's width left of its anchor, the menu's centre
    // x; seen in PCSX2 at level80 checkpoint 2 (no box, the name centred under the menu).
    WarCommandDisplay display;
    Canvas canvas;
    const WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    canvas.draw(display, 0);
    // part_page0 holds the six icons and nothing else.
    REQUIRE(canvas.parts.sprites().size() == 6);
    REQUIRE_FALSE(canvas.text.sprites().empty());
    // The glyphs' extent is centred on slot 0's icon (at the centre x, offset 0).
    float left = canvas.text.sprites()[0].position.x;
    float right = left;
    for (const auto& glyph : canvas.text.sprites()) {
        left = std::min(left, glyph.position.x - (glyph.width / 2.0F));
        right = std::max(right, glyph.position.x + (glyph.width / 2.0F));
    }
    CHECK(right > left);
    CHECK((left + right) / 2.0F == Catch::Approx(canvas.parts.sprites()[0].position.x).margin(0.01));
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

TEST_CASE("each slot's backing and plate are big_font rectangle 256 and its icon a part_page0 rectangle",
          "[hud][war-commands]") {
    // Expected: hud.md#warrior-command-menu, confirmed (code): WarCommandDisplay_Init (0x001a62c0) sets every plate and
    // backing up with sprite word 0xd0100 and resource instance 6 (big_font), each icon with its rectangle and
    // instance 3 (part_page0).
    constexpr std::array<std::uint32_t, 6> kIcons{0x56, 0x5a, 0x58, 0x55, 0x59, 0x5b};
    for (std::size_t slot = 0; slot < coney::hud::kWarCommandSlots; ++slot) {
        const coney::hud::WarCommandSlotSprites sprites = coney::hud::warCommandSlotSprites(slot);
        CHECK(sprites.backing.word == 0xd0100);
        CHECK(sprites.backing.instance == 6);
        CHECK(sprites.plate.word == 0xd0100);
        CHECK(sprites.plate.instance == 6);
        CHECK(sprites.icon.word == kIcons.at(slot));
        CHECK(sprites.icon.instance == 3);
    }
}

TEST_CASE("the slots' backings and plates are drawn from big_font rectangle 256 at sizes 0.077 and 0.07",
          "[hud][war-commands]") {
    // Expected: hud.md#warrior-command-menu, confirmed (code): WarCommandDisplay_Init (0x001a62c0) gives the plates and
    // backings big_font's rectangle 256 (Page_Rect keeps the low 16 bits of 0xd0100); WarCommandDisplay_Update
    // (0x001a7e48) sizes the plates 0.07 (0x0050d304) and the backings 0.07 x 1.1 (0x0050d314) in overlay units, the
    // width following the rectangle's texel aspect (BaseWidget_UpdateSize, 0x001a21e8, no aspect fix).
    WarCommandDisplay display;
    Canvas canvas;
    const WarCommandDisplay::Chief chief;
    display.open(chief, 0);
    canvas.draw(display, 0);
    CHECK(canvas.flat.sprites().empty());
    REQUIRE(canvas.slots.sprites().size() == 12);
    REQUIRE(canvas.parts.sprites().size() == 6);
    for (std::size_t n = 0; n < 12; ++n) {
        const coney::graphics::Sprite& sprite = canvas.slots.sprites()[n];
        CHECK(sprite.uv.u0 == Catch::Approx(Canvas::kDisc.u0));
        CHECK(sprite.uv.v0 == Catch::Approx(Canvas::kDisc.v0));
        CHECK(sprite.uv.u1 == Catch::Approx(Canvas::kDisc.u1));
        CHECK(sprite.uv.v1 == Catch::Approx(Canvas::kDisc.v1));
        // Backing then plate; the disc's 64 x 32 texels make it twice as wide as tall.
        CHECK(sprite.height == Catch::Approx(n % 2 == 0 ? 0.077F : 0.07F));
        CHECK(sprite.width == Catch::Approx(sprite.height * 2.0F));
        // Centred on its slot's icon.
        const coney::graphics::Sprite& icon = canvas.parts.sprites()[n / 2];
        CHECK(sprite.position.x == Catch::Approx(icon.position.x));
        CHECK(sprite.position.y == Catch::Approx(icon.position.y));
    }
}
