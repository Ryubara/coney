// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/options.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"

using coney::ErrorCode;
using coney::parseOptions;

namespace {
// Parses a fixed list of arguments, sparing each test the span conversion.
template <std::size_t N> auto parse(const std::array<std::string_view, N>& args) {
    return parseOptions(std::span<const std::string_view>(args));
}
} // namespace

// No test name starts with "-": ctest runs each case as `coney_tests "<name>"`, and Catch2 would read a leading "--"
// as one of its own options (a case named "--help ..." would print Catch2's help and pass without running).

TEST_CASE("no arguments runs until the window closes", "[options]") {
    auto result = parseOptions({});
    REQUIRE(result.has_value());
    CHECK_FALSE(result->frameLimit.has_value());
    CHECK_FALSE(result->showHelp);
}

TEST_CASE("giving --frames N sets the frame limit", "[options]") {
    auto result = parse(std::array<std::string_view, 2>{"--frames", "3"});
    REQUIRE(result.has_value());
    CHECK(result->frameLimit == 3);
}

TEST_CASE("giving --frames the largest allowed value is accepted", "[options]") {
    auto result = parse(std::array<std::string_view, 2>{"--frames", "1000000"});
    REQUIRE(result.has_value());
    CHECK(result->frameLimit == coney::kMaxFrameLimit);
}

TEST_CASE("giving --help is recognised", "[options]") {
    auto result = parse(std::array<std::string_view, 1>{"--help"});
    REQUIRE(result.has_value());
    CHECK(result->showHelp);
}

TEST_CASE("giving --frames without a value is an error naming --frames", "[options]") {
    auto result = parse(std::array<std::string_view, 1>{"--frames"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::InvalidArgument);
    CHECK(result.error().message.find("--frames") != std::string::npos);
}

TEST_CASE("giving --frames a value that is not a positive whole number in range is an error", "[options]") {
    for (std::string_view bad : {"abc", "0", "-3", "3x", "", "1000001", "99999999999", " 3"}) {
        CAPTURE(bad);
        auto result = parse(std::array<std::string_view, 2>{"--frames", bad});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
        CHECK(result.error().message.find("--frames") != std::string::npos);
    }
}

TEST_CASE("an unknown option is an error naming it", "[options]") {
    auto result = parse(std::array<std::string_view, 1>{"--fullscreen"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("--fullscreen") != std::string::npos);
}

TEST_CASE("a stray positional argument is an error naming it", "[options]") {
    auto result = parse(std::array<std::string_view, 1>{"game.iso"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("game.iso") != std::string::npos);
}

TEST_CASE("giving --frames twice is an error", "[options]") {
    auto result = parse(std::array<std::string_view, 4>{"--frames", "3", "--frames", "4"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("--frames") != std::string::npos);
}

TEST_CASE("the usage text documents every option", "[options]") {
    CHECK(coney::usageText().find("--frames") != std::string_view::npos);
    CHECK(coney::usageText().find("--help") != std::string_view::npos);
}

TEST_CASE("giving --disc and --load collects the disc and the entries in order", "[options]") {
    auto result = parse(std::array<std::string_view, 6>{"--disc", "H:\\", "--load", "level1.lev", "--load", "0x1"});
    REQUIRE(result.has_value());
    CHECK(result->discPath == "H:\\");
    CHECK(result->loads == std::vector<std::string>{"level1.lev", "0x1"});
}

TEST_CASE("giving --load without --disc is an error naming --disc", "[options]") {
    auto result = parse(std::array<std::string_view, 2>{"--load", "level1.lev"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("--disc") != std::string::npos);
}

TEST_CASE("giving --disc or --load without a value is an error", "[options]") {
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--disc"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--disc", "x", "--load"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--disc", "x", "--disc", "y"}).has_value());
}

TEST_CASE("the usage text documents the disc options", "[options]") {
    CHECK(coney::usageText().find("--disc") != std::string_view::npos);
    CHECK(coney::usageText().find("--load") != std::string_view::npos);
}

TEST_CASE("giving --headless, --view-txd and --screenshot sets them", "[options]") {
    auto viewer = parse(std::array<std::string_view, 8>{"--disc", "H:\\", "--view-txd", "0x1234", "--frames", "3",
                                                        "--screenshot", "out.png"});
    REQUIRE(viewer.has_value());
    CHECK(viewer->viewTxd == "0x1234");
    CHECK(viewer->screenshotPath == "out.png");
    CHECK_FALSE(viewer->headless);
    auto headless = parse(std::array<std::string_view, 3>{"--headless", "--frames", "1"});
    REQUIRE(headless.has_value());
    CHECK(headless->headless);
}

TEST_CASE("the viewer and screenshot options refuse what cannot work", "[options]") {
    // --view-txd needs --disc and a value, once, and does not go with --load.
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--view-txd", "a"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--disc", "x", "--view-txd"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 6>{"--disc", "x", "--view-txd", "a", "--view-txd", "b"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 6>{"--disc", "x", "--view-txd", "a", "--load", "b"}).has_value());
    // --screenshot needs --frames and a window.
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--screenshot", "a.png"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 5>{"--screenshot", "a.png", "--frames", "1", "--headless"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--screenshot"}).has_value());
}

TEST_CASE("--help wins over options that do not go together", "[options]") {
    auto result = parse(std::array<std::string_view, 3>{"--view-txd", "a", "--help"});
    REQUIRE(result.has_value());
    CHECK(result->showHelp);
}

TEST_CASE("the usage text documents the viewer options", "[options]") {
    CHECK(coney::usageText().find("--view-txd") != std::string_view::npos);
    CHECK(coney::usageText().find("--screenshot") != std::string_view::npos);
    CHECK(coney::usageText().find("--headless") != std::string_view::npos);
}

TEST_CASE("the sheet viewer option takes a sheet and needs a disc and no other viewer", "[options]") {
    auto viewer = parse(std::array<std::string_view, 4>{"--disc", "H:\\", "--view-sheet", "menu_system"});
    REQUIRE(viewer.has_value());
    CHECK(viewer->viewSheet == "menu_system");
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--view-sheet", "a"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--disc", "x", "--view-sheet"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 6>{"--disc", "x", "--view-sheet", "a", "--view-txd", "b"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 6>{"--disc", "x", "--view-sheet", "a", "--load", "b"}).has_value());
    CHECK(coney::usageText().find("--view-sheet") != std::string_view::npos);
}

TEST_CASE("the world viewer option takes a name and needs a disc and no other viewer", "[options]") {
    auto viewer = parse(std::array<std::string_view, 4>{"--disc", "H:\\", "--view-world", "level2"});
    REQUIRE(viewer.has_value());
    CHECK(viewer->viewWorld == "level2");
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--view-world", "level2"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--disc", "x", "--view-world"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 6>{"--disc", "x", "--view-world", "a", "--view-sheet", "b"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 6>{"--disc", "x", "--view-world", "a", "--load", "b"}).has_value());
    CHECK(coney::usageText().find("--view-world") != std::string_view::npos);
}

TEST_CASE("the play option takes a level and needs a disc and no viewer", "[options]") {
    auto play = parse(std::array<std::string_view, 4>{"--disc", "H:\\", "--play-level", "level99"});
    REQUIRE(play.has_value());
    CHECK(play->playLevel == "level99");
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--play-level", "level99"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--disc", "x", "--play-level"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 6>{"--disc", "x", "--play-level", "a", "--view-world", "b"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 5>{"--disc", "x", "--play-level", "a", "--view-character"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 6>{"--disc", "x", "--play-level", "a", "--render-references", "o"})
                    .has_value());
    CHECK(coney::usageText().find("--play-level") != std::string_view::npos);
}

TEST_CASE("the input script option takes a file and may be given once", "[options]") {
    auto scripted = parse(std::array<std::string_view, 3>{"--input-script", "menu.txt", "--headless"});
    REQUIRE(scripted.has_value());
    CHECK(scripted->inputScript == "menu.txt");
    CHECK(scripted->headless);
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--input-script"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--input-script", "a", "--input-script", "b"}).has_value());
    CHECK(coney::usageText().find("--input-script FILE") != std::string_view::npos);
}

TEST_CASE("the text viewer option takes a font and a text and needs a disc", "[options]") {
    auto viewer = parse(std::array<std::string_view, 5>{"--disc", "H:\\", "--view-text", "big_font", "@0x1f"});
    REQUIRE(viewer.has_value());
    if (!viewer) {
        return;
    }
    // A local copy: clang-tidy cannot follow a check through the outer optional.
    const std::optional<coney::TextView> view = viewer->viewText;
    REQUIRE(view.has_value());
    if (!view) {
        return;
    }
    CHECK(view->font == "big_font");
    CHECK(view->text == "@0x1f");
    CHECK(viewer->language == coney::Language::English);
    CHECK(!parse(std::array<std::string_view, 3>{"--view-text", "big_font", "a"}).has_value());
    CHECK(!parse(std::array<std::string_view, 4>{"--disc", "x", "--view-text", "big_font"}).has_value());
    CHECK(!parse(std::array<std::string_view, 7>{"--disc", "x", "--view-text", "f", "a", "--view-sheet", "b"})
               .has_value());
    CHECK(coney::usageText().find("--view-text FONT TEXT") != std::string_view::npos);
}

TEST_CASE("the language option takes one of the five codes", "[options]") {
    auto german = parse(std::array<std::string_view, 2>{"--language", "de"});
    REQUIRE(german.has_value());
    CHECK(german->language == coney::Language::German);
    CHECK(!parse(std::array<std::string_view, 2>{"--language", "jp"}).has_value());
    CHECK(!parse(std::array<std::string_view, 1>{"--language"}).has_value());
    CHECK(!parse(std::array<std::string_view, 4>{"--language", "en", "--language", "fr"}).has_value());
}

TEST_CASE("the character viewer takes a name or defaults to Rembrandt, and a clip with the anim option", "[options]") {
    auto named =
        parse(std::array<std::string_view, 6>{"--disc", "x", "--view-character", "warr_ty_cv", "--anim", "408"});
    REQUIRE(named.has_value());
    CHECK(named->viewCharacter.value_or("") == "warr_ty_cv");
    CHECK(named->animClip.value_or("") == "408");
    // Without a name, or with another option next, the default.
    auto bare = parse(std::array<std::string_view, 5>{"--disc", "x", "--view-character", "--frames", "3"});
    REQUIRE(bare.has_value());
    CHECK(bare->viewCharacter.value_or("") == coney::kDefaultViewCharacter);
    CHECK(bare->frameLimit == 3);
    CHECK(!parse(std::array<std::string_view, 1>{"--view-character"}).has_value());
    CHECK(!parse(std::array<std::string_view, 4>{"--disc", "x", "--anim", "walk"}).has_value());
    CHECK(!parse(std::array<std::string_view, 5>{"--disc", "x", "--view-character", "--view-world", "level2"})
               .has_value());
    CHECK(coney::usageText().find("--view-character [NAME]") != std::string_view::npos);
}

TEST_CASE("the reference renderer takes a folder, characters to render and a name list, and needs a disc",
          "[options]") {
    auto all = parse(std::array<std::string_view, 4>{"--disc", "x", "--render-references", "out"});
    REQUIRE(all.has_value());
    CHECK(all->renderReferences.value_or("") == "out");
    CHECK(all->only.empty());
    CHECK_FALSE(all->namesFile.has_value());
    auto some = parse(std::array<std::string_view, 10>{"--disc", "x", "--render-references", "out", "--only",
                                                       "warr_re_cv", "--only", "0x1234abcd", "--names", "names.txt"});
    REQUIRE(some.has_value());
    CHECK(some->only == std::vector<std::string>{"warr_re_cv", "0x1234abcd"});
    CHECK(some->namesFile.value_or("") == "names.txt");

    CHECK(!parse(std::array<std::string_view, 2>{"--render-references", "out"}).has_value());
    CHECK(!parse(std::array<std::string_view, 2>{"--only", "warr_re_cv"}).has_value());
    CHECK(
        !parse(std::array<std::string_view, 5>{"--disc", "x", "--render-references", "out", "--headless"}).has_value());
    CHECK(!parse(std::array<std::string_view, 6>{"--disc", "x", "--render-references", "out", "--frames", "3"})
               .has_value());
    CHECK(!parse(std::array<std::string_view, 5>{"--disc", "x", "--render-references", "out", "--view-character"})
               .has_value());
    CHECK(coney::usageText().find("--render-references DIR") != std::string_view::npos);
}

TEST_CASE("the frame pacing options default to no cap with vsync on, outside test mode", "[options]") {
    auto plain = parseOptions({});
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->fpsCap.has_value());
    CHECK(plain->vsync);
    CHECK_FALSE(plain->showFps);
    CHECK_FALSE(coney::isTestMode(*plain));

    auto paced = parse(std::array<std::string_view, 5>{"--fps-cap", "144", "--vsync", "off", "--show-fps"});
    REQUIRE(paced.has_value());
    CHECK(paced->fpsCap == 144);
    CHECK_FALSE(paced->vsync);
    CHECK(paced->showFps);
    auto uncapped = parse(std::array<std::string_view, 2>{"--fps-cap", "0"});
    REQUIRE(uncapped.has_value());
    CHECK(uncapped->fpsCap == 0);
}

TEST_CASE("the frame pacing options refuse bad values and test mode", "[options]") {
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--fps-cap", "-1"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--fps-cap", "1001"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--fps-cap", "60fps"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--fps-cap"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--vsync", "maybe"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--vsync", "on", "--vsync", "off"}).has_value());
    // Test mode is lockstep with no clock: a cap or a rate report means nothing there.
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--fps-cap", "60", "--frames", "3"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--show-fps", "--headless"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--vsync", "off", "--headless"}).has_value());
    auto script = parse(std::array<std::string_view, 2>{"--input-script", "a.txt"});
    REQUIRE(script.has_value());
    CHECK(coney::isTestMode(*script));
    CHECK(coney::usageText().find("--fps-cap") != std::string_view::npos);
}

TEST_CASE("the sandbox option takes a layout or defaults to the default one, and needs no disc", "[options][sandbox]") {
    auto bare = parse(std::array<std::string_view, 1>{"--sandbox"});
    REQUIRE(bare.has_value());
    CHECK(bare->sandbox == std::string(coney::kDefaultSandbox));
    auto named = parse(std::array<std::string_view, 4>{"--sandbox", "parkour", "--frames", "2"});
    REQUIRE(named.has_value());
    CHECK(named->sandbox == "parkour");
    CHECK(named->frameLimit == 2);
    // An option next is not a name.
    auto followed = parse(std::array<std::string_view, 2>{"--sandbox", "--headless"});
    REQUIRE(followed.has_value());
    CHECK(followed->sandbox == std::string(coney::kDefaultSandbox));
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--sandbox", "--sandbox"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 5>{"--sandbox", "--disc", "x", "--play-level", "a"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--sandbox", "--disc", "x", "--view-character"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 5>{"--sandbox", "--disc", "x", "--load", "a"}).has_value());
    CHECK(coney::usageText().find("--sandbox") != std::string_view::npos);
}

TEST_CASE("the play option names a sandbox as sandbox or sandbox:NAME, with a spawn point", "[options][sandbox]") {
    CHECK(coney::sandboxOfPlayLevel("sandbox") == std::string(coney::kDefaultSandbox));
    CHECK(coney::sandboxOfPlayLevel("sandbox:parkour") == "parkour");
    CHECK_FALSE(coney::sandboxOfPlayLevel("sandbox:").has_value());
    CHECK_FALSE(coney::sandboxOfPlayLevel("sandboxes").has_value());
    CHECK_FALSE(coney::sandboxOfPlayLevel("level99").has_value());

    auto play =
        parse(std::array<std::string_view, 6>{"--disc", "x", "--play-level", "sandbox:parkour", "--spawn", "lane"});
    REQUIRE(play.has_value());
    CHECK(play->playLevel == "sandbox:parkour");
    CHECK(play->spawn == "lane");
    // A spawn point belongs to a sandbox.
    CHECK_FALSE(
        parse(std::array<std::string_view, 6>{"--disc", "x", "--play-level", "level99", "--spawn", "a"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--spawn", "a"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 5>{"--disc", "x", "--play-level", "sandbox", "--spawn"}).has_value());
}

TEST_CASE("the assets option takes a folder", "[options][sandbox]") {
    auto assets = parse(std::array<std::string_view, 3>{"--sandbox", "--assets", "some/folder"});
    REQUIRE(assets.has_value());
    CHECK(assets->assetsDir == "some/folder");
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--assets"}).has_value());
    CHECK(coney::usageText().find("--assets") != std::string_view::npos);
}

TEST_CASE("the tunables option takes the debug menus' overrides file once", "[options]") {
    auto tuned = parse(std::array<std::string_view, 2>{"--tunables", "my.ini"});
    REQUIRE(tuned.has_value());
    CHECK(tuned->tunablesFile == "my.ini");
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--tunables"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--tunables", "a", "--tunables", "b"}).has_value());
    CHECK(coney::usageText().find("--tunables FILE") != std::string_view::npos);
}
