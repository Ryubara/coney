// SPDX-License-Identifier: GPL-3.0-or-later
// The level loading screen (docs/research/level-loading.md#level-screen): the picture search with its 16:9, language
// and default fallbacks, the timeline's picture, alpha and bar, the bar's colour and place, the sound sets, and what a
// tick draws. Synthetic archives and sheets; nothing from the disc.
#include "gamemodes/loading_screen.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "graphics/overlay_camera.h"
#include "graphics/screen.h"
#include "hud/spinner.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::Language;
using coney::LoadingScreen;
using coney::LoadScreenTimeline;
using coney::graphics::Rgba;

namespace {

// An archive holding exactly `names`.
std::function<bool(std::string_view)> archive(std::set<std::string, std::less<>> names) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    return [held = std::move(names)](std::string_view name) { return held.contains(name); };
}

// level99's pictures as the disc holds them: three, each with a `_w` twin, no language forms.
std::set<std::string, std::less<>> level99Names() {
    return {"level99_ls_0",   "level99_ls_1",   "level99_ls_2", "level99_ls_0_w",
            "level99_ls_1_w", "level99_ls_2_w", "default_ls_0"};
}

} // namespace

TEST_CASE("loading screen: a story level shows its three pictures, 16:9 picking the _w forms", "[loading_screen]") {
    const auto exists = archive(level99Names());
    const coney::LoadScreenPictures plain =
        coney::loadScreenPictures("level99", 99, 0, Language::English, false, exists);
    CHECK(plain.names == std::vector<std::string>{"level99_ls_0", "level99_ls_1", "level99_ls_2"});
    CHECK_FALSE(plain.fellBack);
    const coney::LoadScreenPictures wide = coney::loadScreenPictures("level99", 99, 0, Language::French, true, exists);
    CHECK(wide.names == std::vector<std::string>{"level99_ls_0_w", "level99_ls_1_w", "level99_ls_2_w"});
}

TEST_CASE("loading screen: the language's form wins over the plain one", "[loading_screen]") {
    const auto exists = archive({"level5_ls_0", "level5_ls_0_sp", "level5_ls_1", "level5_ls_0_w_sp", "level5_ls_0_w"});
    CHECK(coney::loadScreenPictureName("level5", 0, Language::Spanish, false, exists) == "level5_ls_0_sp");
    CHECK(coney::loadScreenPictureName("level5", 0, Language::Spanish, true, exists) == "level5_ls_0_w_sp");
    CHECK(coney::loadScreenPictureName("level5", 1, Language::Spanish, false, exists) == "level5_ls_1");
    // No level5_ls_1_w: step 3 gives picture 0's 16:9 name, so the search stops after one picture.
    CHECK(coney::loadScreenPictureName("level5", 1, Language::English, true, exists) == "level5_ls_0_w");
    CHECK(coney::loadScreenPictures("level5", 5, 0, Language::English, true, exists).names.size() == 1);
}

TEST_CASE("loading screen: one picture gives one, none the default", "[loading_screen]") {
    const auto one = coney::loadScreenPictures("level60", 60, 0, Language::English, false,
                                               archive({"level60_ls_0", "default_ls_0"}));
    CHECK(one.names == std::vector<std::string>{"level60_ls_0"});
    CHECK_FALSE(one.fellBack);
    // level100 has none: default_ls_0, once, with the fallback flag.
    bool fellBack = false;
    CHECK(coney::loadScreenPictureName("level100", 0, Language::English, false, archive({"default_ls_0"}), &fellBack) ==
          "default_ls_0");
    CHECK(fellBack);
    const auto none = coney::loadScreenPictures("level100", 100, 0, Language::English, false, archive({}));
    CHECK(none.names == std::vector<std::string>{"default_ls_0"});
    CHECK(none.fellBack);
    // A 4:3-only level in 16:9 has no _w name at any step, so it shows the default (as the search is written).
    CHECK(coney::loadScreenPictures("level7", 7, 0, Language::English, true, archive({"level7_ls_0"})).names ==
          std::vector<std::string>{"default_ls_0"});
}

TEST_CASE("loading screen: a Rumble arena shows rumble_<game type>, else the default", "[loading_screen]") {
    const auto exists = archive({"rumble_12_ls_0", "rumble_12_ls_0_w_ge", "level102_ls_0"});
    CHECK(coney::loadScreenPictures("level102", 102, 12, Language::English, false, exists).names ==
          std::vector<std::string>{"rumble_12_ls_0"});
    CHECK(coney::loadScreenPictures("level102", 102, 12, Language::German, true, exists).names ==
          std::vector<std::string>{"rumble_12_ls_0_w_ge"});
    const auto other = coney::loadScreenPictures("level102", 102, 7, Language::English, false, exists);
    CHECK(other.names == std::vector<std::string>{"default_ls_0"});
    CHECK(other.fellBack);
}

TEST_CASE("loading screen: the timeline is 23 s for story levels and 30 s for arenas", "[loading_screen]") {
    CHECK(coney::loadScreenTimelineMilliseconds(99) == 23000);
    CHECK(coney::loadScreenTimelineMilliseconds(100) == 23000);
    CHECK(coney::loadScreenTimelineMilliseconds(101) == 30000);
}

TEST_CASE("loading screen: pictures cut by thirds, the bar is a clock, the alpha fades 200 ms", "[loading_screen]") {
    const LoadScreenTimeline timeline{.start = 1000, .end = 24000};
    CHECK(timeline.picture(1000, 3) == 0);
    CHECK(timeline.picture(1000 + 7666, 3) == 0);
    CHECK(timeline.picture(1000 + 7667, 3) == 1);
    CHECK(timeline.picture(1000 + 18000, 3) == 2);
    CHECK(timeline.picture(90000, 3) == 2); // past the end: the last picture
    CHECK(timeline.picture(5000, 1) == 0);
    CHECK(timeline.progress(1000) == 0.0F);
    CHECK(timeline.progress(1000 + 7000) == Approx(7.0 / 23.0));
    CHECK(timeline.progress(90000) == 1.0F);
    CHECK(timeline.alpha(1000) == 0);
    CHECK(timeline.alpha(1100) == 127); // 100 × 1.275
    CHECK(timeline.alpha(1200) == 255);
    CHECK(timeline.alpha(12000) == 255);
    CHECK(timeline.alpha(23900) == 127);
    CHECK(timeline.alpha(25000) == 255); // past the end the unsigned difference is huge
}

TEST_CASE("loading screen: the finish moves the end, so the bar and the picture jump", "[loading_screen]") {
    coney::test::RecordingDevice device;
    LoadingScreen screen(device, {}, archive(level99Names()), {}, {}, [](std::string_view) {});
    screen.begin("level99", 99, 0, 0);
    CHECK(screen.timeline().end == 23000);
    // A 3 s load: picture 0, the bar at 3/23; the finish sends the end to 3,200 ms.
    CHECK(screen.timeline().picture(3000, 3) == 0);
    screen.finish(3000);
    CHECK(screen.timeline().end == 3200);
    CHECK(screen.timeline().picture(3000, 3) == 2);
    CHECK(screen.timeline().progress(3000) == Approx(3000.0 / 3200.0));
    CHECK(screen.timeline().alpha(3000) == 255);
    CHECK(screen.timeline().alpha(3100) == 127);
    CHECK_FALSE(screen.finished(3166));
    CHECK(screen.finished(3170));
    screen.end();
    CHECK_FALSE(screen.active());
    CHECK(screen.timeline().end == 0);
}

TEST_CASE("loading screen: the bar is red, grey for five levels, at the 4:3 corner", "[loading_screen]") {
    CHECK(coney::loadScreenBarColour(99) == Rgba{170, 43, 43, 255});
    for (const int grey : {11, 20, 82, 83, 92}) {
        CHECK(coney::loadScreenBarColour(grey) == Rgba{223, 223, 223, 255});
    }
    CHECK(coney::loadScreenBarColour(12) == Rgba{170, 43, 43, 255});
    // Interlaced 4:3: the corner at (0.528, 0.828) of the screen, 273 × p pixels wide, 8 tall.
    const auto bar = coney::loadScreenBarQuad(coney::graphics::OverlayCamera(), false, 0.5F, Rgba{});
    CHECK(bar.x / coney::graphics::kLogicalWidth == Approx(0.5 + 0.04 / 1.45).margin(1e-4));
    CHECK(bar.y / coney::graphics::kLogicalHeight == Approx(0.828).margin(1e-4));
    CHECK(bar.width == Approx(136.5));
    CHECK(bar.height == 8.0F);
    CHECK(coney::loadScreenBarQuad(coney::graphics::OverlayCamera(), false, 2.0F, Rgba{}).width == Approx(273.0));
    CHECK(coney::loadScreenBarQuad(coney::graphics::OverlayCamera(), true, 1.0F, Rgba{}).width == Approx(212.0));
}

TEST_CASE("loading screen: the sounds start and stop through the hooks, which may be empty", "[loading_screen]") {
    int starts = 0;
    int stops = 0;
    coney::test::RecordingDevice device;
    LoadingScreen screen(device, {}, {}, {},
                         coney::LoadScreenSounds{.start = [&starts] { ++starts; }, .stop = [&stops] { ++stops; }},
                         [](std::string_view) {});
    screen.startSounds();
    screen.stopSounds();
    screen.startSounds();
    CHECK(starts == 2);
    CHECK(stops == 1);
    LoadingScreen silent(device, {}, {}, {}, {}, [](std::string_view) {});
    silent.startSounds();
    silent.stopSounds();
}

TEST_CASE("loading screen: a tick draws black, the picture with the alpha, then the bar", "[loading_screen]") {
    coney::test::RecordingDevice device;
    auto texture = std::make_shared<coney::test::FakeTexture>(512, 512);
    std::vector<std::string> loaded;
    const LoadingScreen::SheetLoader loader =
        [&loaded, &texture](std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
        loaded.emplace_back(name);
        coney::graphics::SpriteSheet sheet;
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 1.0F, 0.875F});
        sheet.texture = texture;
        return sheet;
    };
    LoadingScreen screen(device, loader, archive(level99Names()), {}, {}, [](std::string_view) {});
    screen.begin("level99", 99, 0, 0);
    CHECK(loaded == std::vector<std::string>{"level99_ls_0", "level99_ls_1", "level99_ls_2"});
    REQUIRE(screen.active());

    // 100 ms in: the picture at half alpha, the red bar with it.
    screen.render(100);
    REQUIRE(device.draws.size() == 2);
    CHECK(device.clears.back() == coney::graphics::kBlack);
    CHECK(device.draws[0].quads.front().colour == Rgba{255, 255, 255, 127});
    CHECK(device.draws[1].quads.front().colour == Rgba{170, 43, 43, 127});
    // 5 ms in: alpha 6, too low for the picture; the bar alone.
    device.draws.clear();
    screen.render(5);
    REQUIRE(device.draws.size() == 1);
    CHECK(device.draws[0].quads.front().colour.a == 6);
}

TEST_CASE("loading screen: a picture that fails to load leaves the bar at full alpha", "[loading_screen]") {
    coney::test::RecordingDevice device;
    std::vector<std::string> log;
    const LoadingScreen::SheetLoader failing =
        [](std::string_view) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
        return coney::fail(coney::ErrorCode::NotFound, "missing");
    };
    LoadingScreen screen(device, failing, archive({}), {}, {},
                         [&log](std::string_view line) { log.emplace_back(line); });
    screen.begin("level11", 11, 0, 0);
    screen.render(100);
    REQUIRE(device.draws.size() == 1);
    CHECK(device.draws[0].quads.front().colour == Rgba{223, 223, 223, 255});
    CHECK(log.size() == 2); // the failed picture and the begin line
}

TEST_CASE("loading screen: the memory-card screen's pictures take the language and 16:9 forms", "[loading_screen]") {
    const auto pictures = coney::memoryCardPictures(
        Language::French, false, archive({"memory_card_screen", "memory_card_screen_fr", "memory_card_loading"}));
    CHECK(pictures.names == std::vector<std::string>{"memory_card_screen_fr", "memory_card_loading"});
    const auto wide = coney::memoryCardPictures(Language::English, true, archive({}));
    CHECK(wide.names == std::vector<std::string>{"memory_card_screen_w", "memory_card_loading_w"});
}

TEST_CASE("loading screen: the memory-card screen shows its second picture after 5 s with the spinner's pulse",
          "[loading_screen]") {
    coney::test::RecordingDevice device;
    auto texture = std::make_shared<coney::test::FakeTexture>(512, 512);
    std::vector<std::string> loaded;
    const LoadingScreen::SheetLoader loader =
        [&loaded, &texture](std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
        loaded.emplace_back(name);
        coney::graphics::SpriteSheet sheet;
        // Enough rectangles for the spinner's 92.
        for (int i = 0; i < 100; ++i) {
            sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 0.25F, 0.25F});
        }
        sheet.texture = texture;
        return sheet;
    };
    LoadingScreen screen(device, loader, archive({"memory_card_screen", "memory_card_loading"}), {}, {},
                         [](std::string_view) {});
    coney::hud::Spinner spinner;
    screen.setSpinner(&spinner);
    screen.beginMemoryCard(1000);
    CHECK(screen.memoryCard());
    CHECK(loaded == std::vector<std::string>{"memory_card_screen", "memory_card_loading", "part_page0"});
    CHECK(screen.timeline().end == 22000);
    // Picture 0 alone in the first 5 s: no bar, no spinner.
    screen.render(3000);
    REQUIRE(device.draws.size() == 1);
    CHECK_FALSE(spinner.shown());
    // Then picture 1 and the spinner on, in the pulse's colour at that moment (full red at the cycle's start).
    device.draws.clear();
    screen.render(6600);
    REQUIRE(device.draws.size() == 2);
    CHECK(spinner.shown());
    CHECK(spinner.colour() == coney::hud::Spinner::pulseColour(6600));
    CHECK(device.draws[1].quads.front().colour == coney::hud::Spinner::pulseColour(6600));
    screen.end();
    CHECK_FALSE(screen.memoryCard());
}
