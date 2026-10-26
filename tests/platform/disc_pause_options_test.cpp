// SPDX-License-Identifier: GPL-3.0-or-later
// A check against the player's own disc that the pause menu's Options screen works as a player reaches it
// (docs/research/pause.md#options): `level99` started as `coney --play-level` starts it, START, the left stick pushed
// most of the way right to the Options item, cross; the stick down twice to Audio, cross (the page loops its music),
// the stick right once (Music 0.9 to 0.95), cross keeps it and ends the loop; the stick up to Camera, cross, the stick
// left (Invert P1 on), triangle puts it back. It runs only when the environment variable CONEY_DISC names the disc and
// skips otherwise; it prints counts only (LEGAL.md).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fileio/wad.h"
#include "gamemodes/pause_mode.h"
#include "gui/pause_menu/option_menu.h"
#include "gui/pause_menu/pause_menu.h"
#include "platform/game_session.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// START; the stick right twice at 70 % (Objectives, Stats, Options), cross; down twice (Lighting, Camera, Audio),
// cross; right once; cross. Then up once (Camera), cross, left, triangle. Frames of play.
constexpr std::string_view kPad = "30 tap start\n"
                                  "80 stick left 70 0\n82 stick left 0 0\n"
                                  "92 stick left 70 0\n94 stick left 0 0\n"
                                  "110 tap cross\n"
                                  "130 stick left 0 -70\n132 stick left 0 0\n"
                                  "142 stick left 0 -70\n144 stick left 0 0\n"
                                  "160 tap cross\n"
                                  "180 stick left 70 0\n182 stick left 0 0\n"
                                  "200 tap cross\n"
                                  "220 stick left 0 70\n222 stick left 0 0\n"
                                  "240 tap cross\n"
                                  "260 stick left -70 0\n262 stick left 0 0\n"
                                  "280 tap triangle\n";
// Frames of play by which each step has happened.
constexpr std::uint64_t kOnOptions = 120;
constexpr std::uint64_t kOnAudio = 170;
constexpr std::uint64_t kMusicUp = 190;
constexpr std::uint64_t kAudioKept = 210;
constexpr std::uint64_t kInverted = 270;
constexpr std::uint64_t kReverted = 290;

} // namespace

TEST_CASE("the disc's pause menu: Options changes the music volume and the camera's inversion through the pad",
          "[disc][pause_menu][options]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    const coney::gui::PauseMenu& menu = run.session().flow().pause().menu();
    const coney::GameState& state = run.session().flow().state();
    const coney::FrontEndServices& services = run.session().flow().services();
    const float musicBefore = state.saved.musicVolume;

    REQUIRE(run.runToPlayFrame(kOnOptions));
    REQUIRE(menu.screen() == coney::gui::PauseScreen::Options);
    const std::size_t titles = menu.screenLines().size();

    REQUIRE(run.runToPlayFrame(kOnAudio));
    CHECK(menu.options().pageOpen());
    CHECK(menu.options().selectedTitle() == static_cast<std::size_t>(coney::gui::OptionPage::Audio));
    const bool previewPlaying = services.musicPlaying(coney::PauseMode::kPreviewMusic);

    REQUIRE(run.runToPlayFrame(kMusicUp));
    const float musicRaised = state.saved.musicVolume;
    REQUIRE(run.runToPlayFrame(kAudioKept));
    CHECK_FALSE(menu.options().pageOpen());
    const bool previewAfter = services.musicPlaying(coney::PauseMode::kPreviewMusic);

    REQUIRE(run.runToPlayFrame(kInverted));
    CHECK(menu.options().selectedTitle() == static_cast<std::size_t>(coney::gui::OptionPage::Camera));
    const bool inverted = state.saved.pads[0].invertCamera;
    REQUIRE(run.runToPlayFrame(kReverted));
    const bool invertedAfterBack = state.saved.pads[0].invertCamera;

    std::printf(
        "  pause options: %zu titles, Audio page music loop %d (after %d), music volume x100 %ld -> %ld, invert "
        "P1 %d then %d after back\n",
        titles, previewPlaying ? 1 : 0, previewAfter ? 1 : 0, std::lround(musicBefore * 100.0F),
        std::lround(musicRaised * 100.0F), inverted ? 1 : 0, invertedAfterBack ? 1 : 0);
    CHECK(titles == coney::gui::kOptionPages);
    CHECK(previewPlaying);
    CHECK_FALSE(previewAfter);
    CHECK(musicBefore == Catch::Approx(0.9F));
    CHECK(musicRaised == Catch::Approx(0.95F));
    CHECK(state.saved.musicVolume == Catch::Approx(0.95F));
    CHECK(inverted);
    CHECK_FALSE(invertedAfterBack);
}
