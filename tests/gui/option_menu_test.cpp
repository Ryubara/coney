// SPDX-License-Identifier: GPL-3.0-or-later
// The pause menu's Options screen (docs/research/pause.md#options), driven command by command with synthetic strings.
#include "gui/pause_menu/option_menu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "graphics/overlay_camera.h"
#include "graphics/screen.h"
#include "graphics/sprite_batch.h"
#include "gui/colour_table.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/widget.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::gui::MenuCommand;
using coney::gui::OptionItemKind;
using coney::gui::OptionLayout;
using coney::gui::OptionMenu;
using coney::gui::OptionPage;
using coney::gui::OptionValues;

namespace {

// The screen opened in level 99 over strings that read "#<hex id>", recording its cues and every value applied.
struct Screen {
    coney::gui::GlobalStrings strings;
    coney::gui::MenuInput input;
    OptionMenu menu;
    std::vector<int> cues;
    std::vector<OptionValues> applied;
    std::vector<std::pair<bool, float>> music;
    std::vector<int> vibrations; // a pad's preview rumble, or -1 for the preview stopped

    explicit Screen(OptionValues values = {}, int level = 99, OptionLayout layout = {}) {
        for (std::uint32_t id = 0x30; id < 0x180; ++id) {
            strings.set(id, std::format("#{:x}", id));
        }
        OptionMenu::Hooks hooks;
        hooks.playCue = [this](int cue) { cues.push_back(cue); };
        hooks.apply = [this](const OptionValues& v) { applied.push_back(v); };
        hooks.previewMusic = [this](bool on, float volume) { music.emplace_back(on, volume); };
        hooks.vibrate = [this](int pad, int strength, std::uint64_t ms) {
            CHECK(strength == 120);
            CHECK(ms == 500);
            vibrations.push_back(pad);
        };
        hooks.stopVibration = [this] { vibrations.push_back(-1); };
        menu.open(&strings, input, hooks, values, layout, level, 10'000);
    }
    // One command; returns whether it left the screen.
    bool command(MenuCommand command) { return menu.handle(command); }
    // Down `n` times on the title list, then accept: opens page `page`.
    void openPage(OptionPage page) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(page); ++i) {
            (void)command(MenuCommand::Down);
        }
        (void)command(MenuCommand::Accept);
        REQUIRE(menu.pageOpen());
        REQUIRE(menu.selectedTitle() == static_cast<std::size_t>(page));
    }
    [[nodiscard]] int lastCue() const { return cues.empty() ? -1 : cues.back(); }
};

} // namespace

TEST_CASE("options: seven titles at (0.26, 0.30 + 0.055 i) in big_font at 1.3, the first selected",
          "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    CHECK(screen.menu.lines() == std::vector<std::string>{"#117", "#119", "#122", "#123", "#124", "#11f", "#128"});
    for (std::size_t i = 0; i < coney::gui::kOptionPages; ++i) {
        const auto& style = screen.menu.title(i).style();
        CHECK(style.x == Approx(0.26F));
        CHECK(style.y == Approx(0.30F + (0.055F * static_cast<float>(i))));
        CHECK(style.scale == Approx(1.3F));
        CHECK(style.fontSlot == 6);
        CHECK(style.colour == (i == 0 ? coney::gui::kMenuGrey : coney::gui::kDimGrey));
    }
    CHECK(screen.cues.empty());
}

TEST_CASE("options: up and down move the titles, refusing at the ends; back leaves with cue 0xf",
          "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    CHECK_FALSE(screen.command(MenuCommand::Up));
    CHECK(screen.lastCue() == 0xe);
    for (int i = 0; i < 6; ++i) {
        CHECK_FALSE(screen.command(MenuCommand::Down));
        CHECK(screen.lastCue() == 4);
    }
    CHECK(screen.menu.selectedTitle() == 6);
    CHECK_FALSE(screen.command(MenuCommand::Down));
    CHECK(screen.lastCue() == 0xe);
    CHECK(screen.menu.title(6).style().colour == coney::gui::kMenuGrey);
    CHECK(screen.command(MenuCommand::Back));
    CHECK(screen.lastCue() == 0xf);
}

TEST_CASE("options: the pages' items, in order", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    const auto labels = [&](OptionPage page) {
        std::vector<std::uint32_t> out;
        for (const auto& item : screen.menu.items(page)) {
            out.push_back(item->label);
        }
        return out;
    };
    CHECK(labels(OptionPage::Lighting) == std::vector<std::uint32_t>{0x117});
    CHECK(labels(OptionPage::Camera) == std::vector<std::uint32_t>{0x11a, 0x11b, 0x11c, 0x11d, 0x11e});
    CHECK(labels(OptionPage::Audio) == std::vector<std::uint32_t>{0x129, 0x12a, 0x126});
    CHECK(labels(OptionPage::Video) == std::vector<std::uint32_t>{0x123});
    CHECK(labels(OptionPage::Subtitles) == std::vector<std::uint32_t>{0x124});
    CHECK(labels(OptionPage::Vibration) == std::vector<std::uint32_t>{0x120, 0x121});
    CHECK(labels(OptionPage::RestoreDefault) == std::vector<std::uint32_t>{0x128});
    CHECK(screen.menu.items(OptionPage::Audio).at(0)->kind == OptionItemKind::VolumeBar);
    CHECK(screen.menu.items(OptionPage::Lighting).at(0)->kind == OptionItemKind::Brightness);
    // Level 102 has no Split Screen Merge.
    Screen level102({}, 102);
    CHECK(level102.menu.items(OptionPage::Camera).size() == 4);
}

TEST_CASE("options: the Camera rows at y 0.36 + 0.045 i with their answers at x 0.66 (0.76 in German)",
          "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    const auto& camera = screen.menu.items(OptionPage::Camera);
    for (std::size_t i = 0; i < camera.size(); ++i) {
        const auto& label = camera.at(i)->labelText.style();
        CHECK(label.x == Approx(0.26F));
        CHECK(label.y == Approx(0.36F + (0.045F * static_cast<float>(i))));
        CHECK(label.scale == Approx(1.0F));
        CHECK(label.fontSlot == 3);
        CHECK(camera.at(i)->answersText.style().x == Approx(0.66F));
        CHECK(camera.at(i)->answersText.style().fontSlot == 4);
    }
    Screen german({}, 99, OptionLayout::forMode(coney::Language::German, false, false));
    CHECK(german.menu.items(OptionPage::Camera).at(0)->answersText.style().x == Approx(0.76F));
    CHECK(german.menu.items(OptionPage::Vibration).at(0)->answersText.style().x == Approx(0.66F));
}

TEST_CASE("options: the single-item pages put the answers 0.06 under a 1.3 label at (0.26, 0.30)",
          "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    for (const OptionPage page : {OptionPage::Video, OptionPage::Subtitles, OptionPage::RestoreDefault}) {
        const auto& item = *screen.menu.items(page).at(0);
        CHECK(item.labelText.style().y == Approx(0.30F));
        CHECK(item.labelText.style().scale == Approx(1.3F));
        CHECK(item.answersText.style().x == Approx(0.26F));
        CHECK(item.answersText.style().y == Approx(0.36F));
    }
    Screen german({}, 99, OptionLayout::forMode(coney::Language::German, false, false));
    CHECK(german.menu.items(OptionPage::RestoreDefault).at(0)->answersText.style().y == Approx(0.41F));
    // Pro Logic II at (0.26, 0.60), its answers at 0.66.
    const auto& proLogic = *screen.menu.items(OptionPage::Audio).at(2);
    CHECK(proLogic.labelText.style().y == Approx(0.60F));
    CHECK(proLogic.answersText.style().x == Approx(0.66F));
}

TEST_CASE("options: a Camera choice applies at once, the first answer meaning on", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    screen.openPage(OptionPage::Camera);
    CHECK(screen.lastCue() == 8);
    CHECK(screen.menu.lines().front() == "#119");
    // Invert P1 starts on Off (the second answer); left picks On and applies it.
    const auto& invert = *screen.menu.items(OptionPage::Camera).at(0);
    CHECK(invert.choice == 1);
    CHECK_FALSE(screen.command(MenuCommand::Right));
    CHECK(screen.lastCue() == 0xe);
    CHECK(screen.applied.size() == 1);
    CHECK_FALSE(screen.command(MenuCommand::Left));
    CHECK(screen.lastCue() == 4);
    REQUIRE(screen.applied.size() == 2);
    CHECK(screen.applied.back().invertCamera[0]);
    CHECK(screen.menu.values().invertCamera[0]);
    // Down to Auto Adjust P2 and turn it off.
    (void)screen.command(MenuCommand::Down);
    (void)screen.command(MenuCommand::Down);
    (void)screen.command(MenuCommand::Down);
    CHECK(screen.menu.selectedItem() == 3);
    CHECK(screen.menu.items(OptionPage::Camera).at(3)->labelText.style().colour == coney::gui::kMenuGrey);
    CHECK(screen.menu.items(OptionPage::Camera).at(0)->labelText.style().colour == coney::gui::kDimGrey);
    (void)screen.command(MenuCommand::Right);
    CHECK_FALSE(screen.menu.values().autoAdjustCamera[1]);
    CHECK(screen.menu.values().autoAdjustCamera[0]);
    // Accept closes the page keeping both.
    CHECK_FALSE(screen.command(MenuCommand::Accept));
    CHECK(screen.lastCue() == 8);
    CHECK_FALSE(screen.menu.pageOpen());
    CHECK(screen.menu.values().invertCamera[0]);
    CHECK_FALSE(screen.menu.values().autoAdjustCamera[1]);
}

TEST_CASE("options: back on a page puts back the values it had when opened", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    screen.openPage(OptionPage::Camera);
    (void)screen.command(MenuCommand::Left);
    CHECK(screen.menu.values().invertCamera[0]);
    CHECK_FALSE(screen.command(MenuCommand::Back));
    CHECK(screen.lastCue() == 0xf);
    CHECK_FALSE(screen.menu.pageOpen());
    CHECK_FALSE(screen.menu.values().invertCamera[0]);
    CHECK_FALSE(screen.applied.back().invertCamera[0]);
    CHECK(screen.menu.items(OptionPage::Camera).at(0)->choice == 1);
}

TEST_CASE("options: turning a pad's vibration on rumbles it", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    OptionValues values;
    values.vibration = {false, true};
    Screen screen(values);
    screen.openPage(OptionPage::Vibration);
    (void)screen.command(MenuCommand::Left);
    CHECK(screen.menu.values().vibration[0]);
    CHECK(screen.vibrations == std::vector<int>{0});
    // Off does not rumble.
    (void)screen.command(MenuCommand::Down);
    (void)screen.command(MenuCommand::Right);
    CHECK_FALSE(screen.menu.values().vibration[1]);
    CHECK(screen.vibrations.size() == 1);
}

TEST_CASE("options: the volume bars step 0.05 within 0-1 and the Audio page loops the music", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    screen.openPage(OptionPage::Audio);
    REQUIRE_FALSE(screen.music.empty());
    CHECK(screen.music.back() == std::pair<bool, float>{true, 0.9F});
    (void)screen.command(MenuCommand::Right);
    CHECK(screen.menu.values().musicVolume == Approx(0.95F));
    (void)screen.command(MenuCommand::Right);
    CHECK(screen.menu.values().musicVolume == Approx(1.0F));
    CHECK(screen.lastCue() == 4);
    (void)screen.command(MenuCommand::Right);
    CHECK(screen.lastCue() == 0xe);
    CHECK(screen.menu.values().musicVolume == Approx(1.0F));
    CHECK(screen.music.back().second == Approx(1.0F));
    // SoundFX down.
    (void)screen.command(MenuCommand::Down);
    (void)screen.command(MenuCommand::Left);
    CHECK(screen.menu.values().soundFxVolume == Approx(0.85F));
    // Back restores both and stops the loop.
    (void)screen.command(MenuCommand::Back);
    CHECK(screen.menu.values().musicVolume == Approx(0.9F));
    CHECK(screen.menu.values().soundFxVolume == Approx(0.9F));
    CHECK_FALSE(screen.music.back().first);
}

TEST_CASE("options: brightness steps 5 within 0-100", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    OptionValues values;
    values.brightness = 95;
    Screen screen(values);
    screen.openPage(OptionPage::Lighting);
    const auto& bar = *screen.menu.items(OptionPage::Lighting).at(0);
    CHECK(bar.bar == Approx(0.95F));
    CHECK(bar.labelText.style().y == Approx(0.29F));
    CHECK(bar.labelText.style().scale == Approx(1.3F));
    CHECK(screen.menu.hint().style().y == Approx(0.35F));
    CHECK(screen.menu.hint().text() == "#118");
    (void)screen.command(MenuCommand::Right);
    CHECK(screen.menu.values().brightness == 100);
    (void)screen.command(MenuCommand::Right);
    CHECK(screen.lastCue() == 0xe);
    CHECK(screen.menu.values().brightness == 100);
    (void)screen.command(MenuCommand::Left);
    (void)screen.command(MenuCommand::Left);
    CHECK(screen.menu.values().brightness == 90);
    CHECK_FALSE(screen.command(MenuCommand::Accept));
    CHECK(screen.menu.values().brightness == 90);
}

TEST_CASE("options: Video moves without applying; accept applies Wide", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    screen.openPage(OptionPage::Video);
    const auto& video = *screen.menu.items(OptionPage::Video).at(0);
    CHECK(video.choice == 0);
    (void)screen.command(MenuCommand::Right);
    CHECK(screen.lastCue() == 4);
    CHECK_FALSE(screen.menu.values().wideVideo);
    CHECK(screen.applied.empty());
    (void)screen.command(MenuCommand::Accept);
    CHECK(screen.menu.values().wideVideo);
    CHECK_FALSE(screen.menu.pageOpen());
}

TEST_CASE("options: Restore Default starts on No; Yes restores the defaults, keeping the video mode",
          "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    OptionValues values;
    values.brightness = 70;
    values.musicVolume = 0.3F;
    values.invertCamera = {true, true};
    values.subtitles = true;
    values.wideVideo = true;
    Screen screen(values);
    screen.openPage(OptionPage::RestoreDefault);
    CHECK(screen.menu.items(OptionPage::RestoreDefault).at(0)->choice == 1);
    // No and accept: nothing changes.
    (void)screen.command(MenuCommand::Accept);
    CHECK(screen.menu.values() == values);
    // Yes and accept.
    (void)screen.command(MenuCommand::Accept);
    (void)screen.command(MenuCommand::Left);
    (void)screen.command(MenuCommand::Accept);
    CHECK(screen.menu.values() == OptionValues::defaults(true));
    CHECK(screen.menu.values().brightness == 40);
    CHECK(screen.menu.values().musicVolume == Approx(0.9F));
    CHECK_FALSE(screen.menu.values().invertCamera[0]);
    // The pages show the new values.
    CHECK(screen.menu.items(OptionPage::Camera).at(0)->choice == 1);
    CHECK(screen.menu.items(OptionPage::Lighting).at(0)->bar == Approx(0.4F));
}

TEST_CASE("options: the layout by video mode", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    const OptionLayout wide = OptionLayout::forMode(coney::Language::English, true, false);
    CHECK(wide.titleY == Approx(0.24F));
    CHECK(wide.rowY == Approx(0.30F));
    CHECK(wide.squaresY == Approx(0.34F));
    CHECK(wide.volumeRows[0] == Approx(0.31F));
    CHECK(wide.volumeRows[3] == Approx(0.43F));
    const OptionLayout pal = OptionLayout::forMode(coney::Language::English, false, true);
    CHECK(pal.titleY == Approx(0.32F));
    CHECK(pal.titleStep == Approx(0.045F));
    CHECK(pal.rowStep == Approx(0.04F));
    CHECK(pal.squareScale == Approx(0.7F));
}

TEST_CASE("options: left and right on the title list are refused with cue 0xe", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    CHECK_FALSE(screen.command(MenuCommand::Left));
    CHECK(screen.lastCue() == 0xe);
    CHECK_FALSE(screen.command(MenuCommand::Right));
    CHECK(screen.cues == std::vector<int>{0xe, 0xe});
    CHECK(screen.menu.selectedTitle() == 0);
    CHECK_FALSE(screen.menu.pageOpen());
}

TEST_CASE("options: a page keeps its selected item between visits", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    screen.openPage(OptionPage::Camera);
    (void)screen.command(MenuCommand::Down);
    (void)screen.command(MenuCommand::Down);
    CHECK(screen.menu.selectedItem() == 2);
    (void)screen.command(MenuCommand::Back);
    REQUIRE_FALSE(screen.menu.pageOpen());
    (void)screen.command(MenuCommand::Accept);
    REQUIRE(screen.menu.pageOpen());
    CHECK(screen.menu.selectedItem() == 2);
    CHECK(screen.menu.items(OptionPage::Camera).at(2)->labelText.style().colour == coney::gui::kMenuGrey);
    // Another page has its own selection, from its first item.
    (void)screen.command(MenuCommand::Accept);
    for (int i = 0; i < 4; ++i) {
        (void)screen.command(MenuCommand::Down);
    }
    (void)screen.command(MenuCommand::Accept);
    REQUIRE(screen.menu.selectedTitle() == static_cast<std::size_t>(OptionPage::Vibration));
    CHECK(screen.menu.selectedItem() == 0);
}

TEST_CASE("options: Restore Default is on No each time its page opens", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    screen.openPage(OptionPage::RestoreDefault);
    (void)screen.command(MenuCommand::Left);
    CHECK(screen.menu.items(OptionPage::RestoreDefault).at(0)->choice == 0);
    // Back keeps the page's answer (Restore Default is not reverted), but opening the page again picks No.
    (void)screen.command(MenuCommand::Back);
    CHECK(screen.menu.items(OptionPage::RestoreDefault).at(0)->choice == 0);
    (void)screen.command(MenuCommand::Accept);
    REQUIRE(screen.menu.pageOpen());
    CHECK(screen.menu.items(OptionPage::RestoreDefault).at(0)->choice == 1);
}

TEST_CASE("options: back on Vibration stops the preview and puts back the values it had when opened",
          "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    OptionValues values;
    values.vibration = {false, true};
    Screen screen(values);
    screen.openPage(OptionPage::Vibration);
    (void)screen.command(MenuCommand::Left);
    (void)screen.command(MenuCommand::Down);
    (void)screen.command(MenuCommand::Right);
    CHECK(screen.menu.values().vibration == std::array<bool, 2>{true, false});
    REQUIRE(screen.vibrations == std::vector<int>{0});
    (void)screen.command(MenuCommand::Back);
    CHECK(screen.lastCue() == 0xf);
    CHECK_FALSE(screen.menu.pageOpen());
    CHECK(screen.menu.values().vibration == std::array<bool, 2>{false, true});
    REQUIRE_FALSE(screen.applied.empty());
    CHECK(screen.applied.back().vibration == std::array<bool, 2>{false, true});
    // The preview stops first; putting P2 back on rumbles it again.
    CHECK(screen.vibrations == std::vector<int>{0, -1, 1});
}

TEST_CASE("options: a refused choice or bar step still calls the item's setter", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    screen.openPage(OptionPage::Vibration);
    // P1's vibration is already on: left is refused (cue 0xe) but sets it on again, which rumbles.
    (void)screen.command(MenuCommand::Left);
    CHECK(screen.lastCue() == 0xe);
    CHECK(screen.applied.size() == 1);
    CHECK(screen.vibrations == std::vector<int>{0});
    // The Music bar at 1 refuses right but sets the volume again.
    OptionValues values;
    values.musicVolume = 1.0F;
    Screen audio(values);
    audio.openPage(OptionPage::Audio);
    (void)audio.command(MenuCommand::Right);
    CHECK(audio.lastCue() == 0xe);
    REQUIRE(audio.applied.size() == 1);
    CHECK(audio.applied.back().musicVolume == Approx(1.0F));
    // Video in confirm mode: a refused move applies nothing.
    Screen video;
    video.openPage(OptionPage::Video);
    (void)video.command(MenuCommand::Left);
    CHECK(video.lastCue() == 0xe);
    CHECK(video.applied.empty());
}

TEST_CASE("options: after Restore Default the title list starts again at Lighting", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    screen.openPage(OptionPage::RestoreDefault);
    (void)screen.command(MenuCommand::Left);
    (void)screen.command(MenuCommand::Accept);
    CHECK_FALSE(screen.menu.pageOpen());
    CHECK(screen.menu.selectedTitle() == 0);
    CHECK(screen.menu.title(0).style().colour == coney::gui::kMenuGrey);
    CHECK(screen.menu.title(6).style().colour == coney::gui::kDimGrey);
    // No keeps the list where it was.
    Screen no;
    no.openPage(OptionPage::RestoreDefault);
    (void)no.command(MenuCommand::Accept);
    CHECK(no.menu.selectedTitle() == 6);
}

TEST_CASE("options: flag 0x02 moves only the Camera answers; with 16:9 too only the squares and answers change",
          "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    const OptionLayout flag02 = OptionLayout::forMode(coney::Language::English, false, true);
    CHECK(flag02.answersOffset == Approx(0.4F));
    CHECK(flag02.cameraAnswersExtra == Approx(0.12F));
    CHECK(flag02.lightingLabelY == Approx(0.33F));
    CHECK(flag02.rowY == Approx(0.37F));
    CHECK(flag02.volumeRows[0] == Approx(0.38F));
    CHECK(flag02.volumeRows[1] == Approx(0.415F));
    CHECK(flag02.volumeRows[2] == Approx(0.455F));
    CHECK(flag02.volumeRows[3] == Approx(0.49F));
    // The flag's Camera answers replace German's.
    CHECK(OptionLayout::forMode(coney::Language::German, false, true).cameraAnswersExtra == Approx(0.12F));
    // Flags 0x02 and 0x04 together: the default places, smaller squares, Camera answers 0.1 further right.
    const OptionLayout both = OptionLayout::forMode(coney::Language::English, true, true);
    CHECK(both.titleY == Approx(0.30F));
    CHECK(both.titleStep == Approx(0.055F));
    CHECK(both.rowY == Approx(0.36F));
    CHECK(both.lightingLabelY == Approx(0.29F));
    CHECK(both.squaresY == Approx(0.38F));
    CHECK(both.volumeRows[0] == Approx(0.37F));
    CHECK(both.squareScale == Approx(0.7F));
    CHECK(both.cameraAnswersExtra == Approx(0.1F));
    CHECK(both.answersOffset == Approx(0.4F));
}

TEST_CASE("options: a move refused at an end stops the d-pad repeating until the next move", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    coney::Pad pad;
    // Holds `buttons` for `samples` pad samples 33 ms apart from `now`, handing each command to the screen.
    const auto hold = [&](std::uint16_t buttons, int samples, std::uint64_t now) {
        int commands = 0;
        for (int i = 0; i < samples; ++i, now += 33) {
            coney::PadSample held;
            held.connected = true;
            held.buttons = buttons;
            held.pressure.fill(255);
            pad.update(held);
            if (const auto command = screen.input.dispatch(pad, now)) {
                ++commands;
                (void)screen.command(*command);
            }
        }
        return commands;
    };
    // Up on the first title is refused once; the held d-pad then gives nothing more.
    CHECK(hold(coney::pad::kUp, 30, 20'000) == 1);
    CHECK(screen.cues == std::vector<int>{0xe});
    (void)hold(0, 2, 21'000);
    // Down moves, and a held Down repeats again.
    CHECK(hold(coney::pad::kDown, 30, 22'000) > 1);
    // On a page: Left on an already-on choice is refused once too.
    Screen page;
    page.openPage(OptionPage::Vibration);
    coney::Pad pad2;
    int commands = 0;
    std::uint64_t now = 30'000;
    for (int i = 0; i < 30; ++i, now += 33) {
        coney::PadSample held;
        held.connected = true;
        held.buttons = coney::pad::kLeft;
        held.pressure.fill(255);
        pad2.update(held);
        if (const auto command = page.input.dispatch(pad2, now)) {
            ++commands;
            (void)page.command(*command);
        }
    }
    CHECK(commands == 1);
}

namespace {

// The background sheet's (record 12) synthetic stand-in: seven rectangles over a 128 x 128 texture, rectangle 0 (the
// bars' sizing sprite) 64 x 16 texels, so a sprite of it is a quarter as tall as it is wide.
coney::graphics::SpriteSheet sheet12() {
    coney::graphics::SpriteSheet sheet;
    constexpr float kTexel = 1.0F / 128.0F;
    sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 64.0F * kTexel, 16.0F * kTexel});
    for (int i = 1; i < 7; ++i) {
        const float v0 = 16.0F * kTexel * static_cast<float>(i);
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, v0, 8.0F * kTexel, v0 + (8.0F * kTexel)});
    }
    sheet.texture = std::make_shared<coney::test::FakeTexture>(128, 128);
    return sheet;
}

// The Audio page drawn with `layout` and `fade` into a synthetic sheet 12 batch and a synthetic part_page0 batch (the
// one every font slot but big_font's draws into); no fonts, so the texts add nothing.
struct DrawnAudio {
    coney::graphics::SpriteBatch sheet{sheet12(), 64, 8500.0F};
    coney::graphics::SpriteBatch parts{coney::test::testFontSheet(), 64, 9000.0F};

    DrawnAudio(const OptionLayout& layout, float fade) {
        Screen screen({}, 99, layout);
        screen.menu.setSpriteBatch(&sheet);
        screen.menu.setFade(fade);
        screen.openPage(OptionPage::Audio);
        coney::gui::GuiCanvas canvas;
        canvas.textBatch = [this](int /*slot*/) { return &parts; };
        screen.menu.render(canvas);
    }
};

} // namespace

TEST_CASE("options: a volume bar draws no frame, a dark back and a fill in its label's colour, as its first update "
          "sizes them",
          "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    using coney::graphics::OverlayCamera;
    for (const bool flag02 : {false, true}) {
        const OptionLayout layout = OptionLayout::forMode(coney::Language::English, false, flag02);
        const DrawnAudio drawn(layout, 1.0F);
        // No sprite of sheet 12: the sizing sprite is never drawn.
        CHECK(drawn.sheet.sprites().empty());
        // Per bar, the back then the fill, in part_page0's strip rectangle (55).
        REQUIRE(drawn.parts.sprites().size() == 4);
        // The height: the sizing sprite's (0.54 wide, its rectangle's shape), less 0.04 (0.025 with flag 0x02).
        const float width = OverlayCamera::guiWidthToOverlay(0.54F);
        const float height = (width * 0.25F) - (flag02 ? 0.025F : 0.04F);
        // The strip starts after the left cap (rectangle 56, 8 x 16 texels drawn that tall) from x 0.26 - 0.001.
        const float cap = height * 0.5F * (coney::graphics::kLogicalHeight / coney::graphics::kLogicalWidth);
        const float inner = 0.54F - cap;
        const std::array<float, 2> rows{layout.volumeRows[1], layout.volumeRows[3]};
        const std::array<coney::graphics::Rgba, 2> fills{coney::gui::kMenuGrey, coney::gui::kDimGrey};
        const coney::graphics::UvRect strip = coney::test::testFontSheet().page.rect(55);
        for (std::size_t bar = 0; bar < 2; ++bar) {
            const coney::graphics::Sprite& back = drawn.parts.sprites().at(bar * 2);
            const coney::graphics::Sprite& fill = drawn.parts.sprites().at((bar * 2) + 1);
            const auto backCentre = OverlayCamera::guiToOverlay(0.259F + cap + (inner / 2.0F), rows.at(bar));
            const auto fillCentre = OverlayCamera::guiToOverlay(0.259F + cap + (inner * 0.9F / 2.0F), rows.at(bar));
            CHECK(back.position.x == Approx(backCentre.x));
            CHECK(back.position.y == Approx(backCentre.y));
            CHECK(fill.position.x == Approx(fillCentre.x));
            CHECK(back.width == Approx(OverlayCamera::guiWidthToOverlay(inner)));
            CHECK(fill.width == Approx(OverlayCamera::guiWidthToOverlay(inner * 0.9F)));
            CHECK(back.height == Approx(height));
            CHECK(fill.height == Approx(height));
            CHECK(back.colour == coney::graphics::Rgba{21, 21, 21, 255});
            const coney::graphics::Rgba label = fills.at(bar);
            CHECK(fill.colour == coney::graphics::Rgba{label.r, label.g, label.b, 255});
            // The strip moved in by a texel across, not down.
            CHECK(back.uv.u0 == Approx(strip.u0 + (1.0F / 128.0F)));
            CHECK(back.uv.u1 == Approx(strip.u1 - (1.0F / 128.0F)));
            CHECK(back.uv.v0 == Approx(strip.v0));
        }
    }
    // Faded, the fill takes the menu's alpha and the back keeps its own.
    const DrawnAudio faded(OptionLayout{}, 0.5F);
    REQUIRE(faded.parts.sprites().size() == 4);
    CHECK(faded.parts.sprites().at(0).colour.a == 255);
    CHECK(faded.parts.sprites().at(1).colour.a == 128);
}

TEST_CASE("options: a choice's answers are set edge to edge, the slash between them", "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    Screen screen;
    const auto tag = [](coney::graphics::Rgba c) {
        return std::format("<COLOR {:02x}{:02x}{:02x}{:02x}>", c.r, c.g, c.b, c.a);
    };
    // Invert P1 is off: the second answer is the chosen one.
    CHECK(screen.menu.items(OptionPage::Camera).at(0)->answersText.text() ==
          std::format("{}#12b</COLOR>/{}#12c</COLOR>", tag(coney::gui::kDimGrey), tag(coney::gui::kMenuGrey)));
    // Restore Default: Yes/No, on No.
    CHECK(screen.menu.items(OptionPage::RestoreDefault).at(0)->answersText.text() ==
          std::format("{}#3b</COLOR>/{}#3c</COLOR>", tag(coney::gui::kDimGrey), tag(coney::gui::kMenuGrey)));
}
