// SPDX-License-Identifier: GPL-3.0-or-later
// The player panel (docs/research/hud.md#the-player-panel): its layout on the 640 x 448 screen, the fade, the counting
// score and money, the counters' slots, the rage meter's values and colours, and its sprites, on synthetic data.
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "graphics/overlay_camera.h"
#include "hud/counting_number.h"
#include "hud/hud_layout.h"
#include "hud/player_panel.h"
#include "support/font_fixtures.h"
#include "support/hud_fixtures.h"
#include "support/optional_value.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::hud::GuiPoint;
using coney::hud::PanelValues;
using coney::hud::PlayerPanel;

using coney::test::KeepingAudio;

namespace {

// Steps `panel` at 1/30 s from `startMs` for `steps` steps with `values`; returns the time after.
std::uint64_t run(PlayerPanel& panel, const PanelValues& values, std::uint64_t startMs, int steps,
                  const coney::hud::HudSound& audio = coney::test::kSilent) {
    std::uint64_t now = startMs;
    for (int i = 0; i < steps; ++i) {
        now += 33;
        panel.update(values, nullptr, now, audio);
    }
    return now;
}

// A sheet of 400 rectangles of 8 x 8 texels over a fake 256 x 256 texture: room for every part_page0 index used.
coney::graphics::SpriteSheet partsSheet() {
    coney::graphics::SpriteSheet sheet;
    for (int i = 0; i < 400; ++i) {
        const float u = static_cast<float>(i % 32) / 32.0F;
        const int level = i / 32;
        const float v = static_cast<float>(level) / 32.0F;
        sheet.page.rects.push_back(coney::graphics::UvRect{u, v, u + 1.0F / 32.0F, v + 1.0F / 32.0F});
    }
    sheet.texture = std::make_shared<coney::test::FakeTexture>(256, 256);
    return sheet;
}

} // namespace

TEST_CASE("the panel's parts land where the runtime measurement put them on the 640 x 448 screen", "[hud]") {
    const coney::graphics::OverlayCamera camera;
    // The rage meter's left end: GUI (-0.007, 0.085), measured at 29 px across and 55 down.
    const GuiPoint meter{coney::hud::kPanelBase[0].x + coney::hud::kMeterOffset[0].x,
                         coney::hud::kPanelBase[0].y + coney::hud::kMeterOffset[0].y};
    const coney::graphics::LogicalPoint at = camera.guiToLogical(meter.x, meter.y);
    CHECK(at.x == Approx(29.0F).margin(1.0F));
    CHECK(at.y == Approx(55.0F).margin(1.0F));
    // The page's formula for the banner (x 30, y 39), the score (31, 67) and the money (36, 83).
    const auto pixels = [&camera](GuiPoint offset) {
        return camera.guiToLogical(coney::hud::kPanelBase[0].x + offset.x, coney::hud::kPanelBase[0].y + offset.y);
    };
    CHECK(pixels(coney::hud::kBannerOffset[0]).x == Approx(30.0F).margin(1.0F));
    CHECK(pixels(coney::hud::kBannerOffset[0]).y == Approx(39.0F).margin(1.0F));
    CHECK(pixels(coney::hud::kScoreOffset).y == Approx(67.0F).margin(1.0F));
    CHECK(pixels(coney::hud::kMoneyOffset).x == Approx(36.0F).margin(1.0F));
    CHECK(pixels(coney::hud::kMoneyOffset).y == Approx(83.0F).margin(1.0F));
    // The meter is 0.30 GUI wide: about 172 px.
    const float width = camera.guiToLogical(meter.x + coney::hud::kMeterSize.width, meter.y).x - at.x;
    CHECK(width == Approx(172.0F).margin(1.0F));
}

TEST_CASE("a shown number counts toward its value by a sixteenth plus one a frame", "[hud]") {
    CHECK(coney::hud::countStep(0, 0) == 0);
    CHECK(coney::hud::countStep(0, 1) == 1);
    CHECK(coney::hud::countStep(0, 160) == 11);
    CHECK(coney::hud::countStep(100, 0) == -7);
    CHECK(coney::hud::countStep(0, -5) == -1);

    coney::hud::CountingNumber number;
    CHECK_FALSE(number.update(50, 0)); // the first value is taken as it is
    CHECK(number.shown() == 50);
    CHECK(number.update(210, 33));
    REQUIRE(number.popup().has_value());
    CHECK(coney::test::got(number.popup()).delta == 160);
    CHECK(number.shown() == 61);
    int frames = 1;
    while (number.counting()) {
        number.update(210, 33 + 33 * static_cast<std::uint64_t>(frames));
        ++frames;
    }
    CHECK(number.shown() == 210);
    CHECK(frames < 60); // about two seconds for a gap of 150
}

TEST_CASE("the panel shows for 2 s after activity and fades out over 1 s", "[hud]") {
    PlayerPanel panel(0);
    REQUIRE(panel.attach(32));
    CHECK_FALSE(panel.attach(32));
    CHECK(panel.shown());
    PanelValues values;
    panel.update(values, nullptr, 0, coney::test::kSilent);
    CHECK(panel.fade() == 1.0F);
    panel.update(values, nullptr, 2000, coney::test::kSilent);
    CHECK(panel.fade() == 1.0F);
    panel.update(values, nullptr, 2500, coney::test::kSilent);
    CHECK(panel.fade() == Approx(0.5F));
    panel.update(values, nullptr, 3000, coney::test::kSilent);
    CHECK(panel.fade() == 0.0F);
    // A changed score is activity again.
    values.score = 100;
    panel.update(values, nullptr, 5000, coney::test::kSilent);
    CHECK(panel.fade() == 1.0F);
    panel.update(values, nullptr, 9000, coney::test::kSilent);
    CHECK(panel.fade() == 0.0F);
    // ForceShowPlayerHud keeps it up.
    panel.setForceShow(true);
    panel.update(values, nullptr, 20000, coney::test::kSilent);
    CHECK(panel.fade() == 1.0F);
}

TEST_CASE("SELECT shows the panel when the player's record allows it", "[hud]") {
    PlayerPanel panel(0);
    panel.attach(32);
    PanelValues values;
    panel.update(values, nullptr, 10000, coney::test::kSilent);
    CHECK(panel.fade() == 0.0F);
    coney::Pad pad;
    pad.update(coney::PadSample{.connected = true, .buttons = coney::pad::kSelect});
    values.selectShows = false;
    panel.update(values, &pad, 10033, coney::test::kSilent);
    CHECK(panel.fade() == 0.0F);
    values.selectShows = true;
    pad.update(coney::PadSample{.connected = true, .buttons = coney::pad::kSelect});
    panel.update(values, &pad, 10066, coney::test::kSilent);
    CHECK(panel.fade() == 0.0F); // still held: not a new press
    pad.update(coney::PadSample{.connected = true});
    pad.update(coney::PadSample{.connected = true, .buttons = coney::pad::kSelect});
    panel.update(values, &pad, 10100, coney::test::kSilent);
    CHECK(panel.fade() == 1.0F);
}

TEST_CASE("the counters' slots move right as the money's digits grow and stay from 1,000", "[hud]") {
    const auto slots = [](int money) { return coney::hud::counterSlots(money); };
    REQUIRE(slots(0).has_value());
    CHECK(coney::test::got(slots(0))[0] == GuiPoint{-0.095F, coney::hud::kSlotLine1});
    CHECK(coney::test::got(slots(-5))[3] == GuiPoint{0.055F, coney::hud::kSlotLine1});
    CHECK(coney::test::got(slots(7))[3] == GuiPoint{-0.09F, coney::hud::kSlotLine2});
    CHECK(coney::test::got(slots(42))[1] == GuiPoint{0.02F, coney::hud::kSlotLine1});
    CHECK(coney::test::got(slots(999))[2] == GuiPoint{-0.092F, coney::hud::kSlotLine2});
    CHECK_FALSE(slots(1000).has_value());

    // The panel keeps its last places at 1,000 or more.
    PlayerPanel panel(0);
    panel.attach(32);
    PanelValues values;
    values.money = 150;
    run(panel, values, 0, 60);
    const auto before = panel.slots();
    values.money = 5000;
    run(panel, values, 2000, 60);
    CHECK(panel.slots() == before);
}

TEST_CASE("money is clamped, counts with its cue and a changed item count is activity", "[hud]") {
    PlayerPanel panel(0);
    panel.attach(32);
    KeepingAudio audio;
    PanelValues values;
    panel.update(values, nullptr, 0, audio.sound);
    values.money = 20000;
    std::uint64_t now = run(panel, values, 0, 1, audio.sound);
    CHECK(panel.money().value() == coney::hud::kMoneyMax);
    CHECK(audio.cues == std::vector<int>{coney::hud::kCueMoneyCount});
    now = run(panel, values, now, 200, audio.sound);
    CHECK(panel.money().shown() == coney::hud::kMoneyMax);
    CHECK_FALSE(panel.money().popup().has_value()); // over after its second
    values.items = {0, 12, 0, 0};
    run(panel, values, now + 5000, 1, audio.sound);
    CHECK(panel.fade() == 1.0F);
}

TEST_CASE("the rage meter shows rage over 150 and a full meter pulses gold and plays its sound once", "[hud]") {
    PlayerPanel panel(0);
    panel.attach(32);
    KeepingAudio audio;
    PanelValues values;
    values.rage = 40;
    values.rageMax = 78;
    panel.update(values, nullptr, 0, audio.sound);
    CHECK(panel.fill() == Approx(40.0F / 150.0F));
    CHECK(panel.capacity() == Approx(0.52F));
    CHECK(panel.fillColour(false) == coney::hud::kRageRed);
    values.raging = true;
    panel.update(values, nullptr, 33, audio.sound);
    CHECK(panel.fillColour(false) == coney::hud::kRageGold);
    CHECK(panel.bannerColour(false) == coney::hud::kRageGold);
    CHECK(panel.fillColour(true) == coney::hud::kRageRed); // player 1 above level 100
    // Full: gold only while |200 - t mod 400|³ / 200³ < 0.2, a short pulse round t = 200.
    values.raging = false;
    values.rage = 90;
    panel.update(values, nullptr, 400, audio.sound);
    CHECK(panel.fill() == Approx(78.0F / 150.0F));
    CHECK(panel.fillColour(false) == coney::hud::kRageRed);
    panel.update(values, nullptr, 600, audio.sound);
    CHECK(panel.fillColour(false) == coney::hud::kRageGold);
    panel.update(values, nullptr, 750, audio.sound);
    CHECK(panel.fillColour(false) == coney::hud::kRageRed);
    CHECK(audio.sounds == std::vector<std::string>{coney::hud::kRageFullSound});
    values.altBanner = true;
    panel.update(values, nullptr, 700, audio.sound);
    CHECK(panel.bannerColour(false) == coney::hud::kBannerAltColour);
}

TEST_CASE("FlashRageBar draws the meter n frames and skips it n frames", "[hud]") {
    PlayerPanel panel(0);
    panel.attach(32);
    panel.setFlashFrames(5);
    std::vector<bool> drawn;
    for (int i = 0; i < 20; ++i) {
        drawn.push_back(panel.meterVisible());
        panel.update(PanelValues{}, nullptr, static_cast<std::uint64_t>(i) * 33, coney::test::kSilent);
    }
    const std::vector<bool> expected{true, true, true, true, true, false, false, false, false, false,
                                     true, true, true, true, true, false, false, false, false, false};
    CHECK(drawn == expected);
    panel.setFlashFrames(0);
    CHECK(panel.meterVisible());
}

TEST_CASE("the banner comes from the character type", "[hud]") {
    CHECK(coney::hud::bannerRecord(32) == 0x2f); // Rembrandt
    CHECK(coney::hud::bannerRecord(0x1e) == 0x2f);
    CHECK(coney::hud::bannerRecord(40) == 0x20);
    CHECK(coney::hud::bannerRecord(0xbe) == 0x30);
    CHECK(coney::hud::bannerRecord(500) == 0x31);
}

TEST_CASE("the panel's sprites: banner with two shadows, the meter, the score's seven digits", "[hud]") {
    coney::graphics::Font font = coney::test::testFont();
    coney::graphics::SpriteBatch text(font.sheet(), 4096, 10000.0F);
    coney::graphics::SpriteBatch parts(partsSheet(), 1024, 10000.0F);
    coney::graphics::SpriteBatch banner(partsSheet(), 16, 11000.0F);
    coney::graphics::SpriteBatch shadows(partsSheet(), 16, 10000.0F);
    coney::hud::HudCanvas canvas;
    canvas.text.fonts = [&font](int) { return &font; };
    canvas.text.textBatch = [&text](int) { return &text; };
    canvas.parts = &parts;
    canvas.banner = [&](std::uint32_t record, bool shadow) {
        CHECK(record == 0x2f);
        return shadow ? &shadows : &banner;
    };
    PlayerPanel panel(0);
    panel.attach(32);
    PanelValues values;
    values.rage = 40;
    values.score = 628;
    panel.update(values, nullptr, 0, coney::test::kSilent);
    panel.render(canvas, 99);
    CHECK(banner.sprites().size() == 1);
    CHECK(shadows.sprites().size() == 2);
    CHECK(banner.sprites()[0].colour == coney::hud::kRageRed);
    // Left cap, body, capacity, fill, right cap.
    CHECK(parts.sprites().size() == 5);
    // Seven digits, each with its shadow.
    CHECK(text.sprites().size() == 14);
    CHECK(text.sprites()[1].colour == coney::hud::kScoreZeroGrey);
    CHECK(text.sprites()[13].colour == coney::graphics::kWhite);
    // No score from level 100; and nothing at all once faded out.
    text.clear();
    panel.render(canvas, 100);
    CHECK(text.sprites().empty());
    parts.clear();
    panel.update(values, nullptr, 5000, coney::test::kSilent);
    panel.render(canvas, 99);
    CHECK(parts.sprites().empty());
}

TEST_CASE("an action prompt naming a dealer's goods wakes the panel", "[hud]") {
    CHECK(coney::hud::promptWakesPanel("Buy Flash $20"));
    CHECK(coney::hud::promptWakesPanel("Spray paint"));
    CHECK_FALSE(coney::hud::promptWakesPanel("Steal"));
    CHECK_FALSE(coney::hud::promptWakesPanel(""));
    PlayerPanel panel(0);
    panel.attach(32);
    PanelValues values;
    panel.update(values, nullptr, 10000, coney::test::kSilent);
    CHECK(panel.fade() == 0.0F);
    values.promptWakes = true;
    panel.update(values, nullptr, 10033, coney::test::kSilent);
    CHECK(panel.fade() == 1.0F);
}

TEST_CASE("the panel's tally sits under the money and the counters, in the rage colour", "[hud][numindicator]") {
    PlayerPanel panel(0);
    panel.attach(32);
    PanelValues values;
    panel.update(values, nullptr, 0, coney::test::kSilent);
    // No money, no counters: the strokes from x 0.005 every 0.015, the bar at 0.025, y 0.154; in Rumble 0.03 higher.
    CHECK(panel.tallyMarkPlace(0, 99).x == Approx(0.005F).margin(1e-5));
    CHECK(panel.tallyMarkPlace(4, 99).x == Approx(0.025F).margin(1e-5));
    CHECK(panel.tallyMarkPlace(5, 99).x == Approx(0.075F).margin(1e-5));
    CHECK(panel.tallyMarkPlace(0, 99).y == Approx(0.154F).margin(1e-5));
    CHECK(panel.tallyMarkPlace(0, 101).y == Approx(0.124F).margin(1e-5));
    // Money on screen: one line down; three counters with money of 100-999 put slot 2 on line 2: two lines.
    values.money = 50;
    panel.update(values, nullptr, 33, coney::test::kSilent);
    CHECK(panel.tallyMarkPlace(0, 99).y == Approx(0.199F).margin(1e-5));
    values.money = 500;
    values.items = {1, 1, 1, 0};
    panel.update(values, nullptr, 66, coney::test::kSilent);
    CHECK(panel.tallyMarkPlace(0, 99).y == Approx(0.244F).margin(1e-5));
    // Player 1's strokes start at 0.779.
    const PlayerPanel other(1);
    CHECK(other.tallyMarkPlace(0, 99).x == Approx(0.779F).margin(1e-5));

    // Drawn while on: the count's marks in red, turned.
    coney::graphics::SpriteBatch parts(partsSheet(), 1024, 10000.0F);
    coney::hud::HudCanvas canvas;
    canvas.parts = &parts;
    panel.setTally(true, 6);
    panel.render(canvas, 99);
    const auto red = std::ranges::count_if(
        parts.sprites(), [](const coney::graphics::Sprite& sprite) { return sprite.colour == coney::hud::kRageRed; });
    CHECK(red == 6);
}
