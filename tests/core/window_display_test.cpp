// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/window_display.h"

#include <array>
#include <cstddef>
#include <optional>

#include <catch2/catch_test_macros.hpp>

using coney::DisplayInfo;
using coney::DisplayRect;
using coney::parseWindowDisplay;
using coney::pickDisplay;
using coney::placeWindow;
using coney::WindowDisplayChoice;

namespace {
// The developer's desk this setting was made for: a 2560 x 1440 primary, a 1080 x 1920 portrait display to its left
// and a 1920 x 1080 one further right, in SDL's order (the primary first). Usable areas leave room for a taskbar.
const std::array<DisplayInfo, 3> kDesk{{
    {"DELL U2724D", {0, 0, 2560, 1440}, {0, 0, 2560, 1392}},
    {"LG HDR 4K", {-1080, 249, 1080, 1920}, {-1080, 249, 1080, 1872}},
    {"Generic PnP Monitor", {3840, 0, 1920, 1080}, {3840, 0, 1920, 1032}},
}};
} // namespace

TEST_CASE("window display: an empty value means no choice", "[core][window_display]") {
    CHECK_FALSE(parseWindowDisplay("").has_value());
    CHECK_FALSE(parseWindowDisplay("  \t").has_value());
}

TEST_CASE("window display: digits are a 1-based index", "[core][window_display]") {
    REQUIRE(parseWindowDisplay(" 2 ").has_value());
    const WindowDisplayChoice choice = parseWindowDisplay(" 2 ").value_or(WindowDisplayChoice{});
    CHECK(choice.kind == WindowDisplayChoice::Kind::Index);
    CHECK(choice.index == 2);
    CHECK(pickDisplay(choice, kDesk) == std::optional<std::size_t>{1});
}

TEST_CASE("window display: an index outside the list picks nothing", "[core][window_display]") {
    for (const char* text : {"0", "4", "99999999999999999999"}) {
        REQUIRE(parseWindowDisplay(text).has_value());
        CHECK_FALSE(pickDisplay(parseWindowDisplay(text).value_or(WindowDisplayChoice{}), kDesk).has_value());
    }
}

TEST_CASE("window display: portrait picks the first display taller than wide", "[core][window_display]") {
    for (const char* text : {"portrait", "Portrait", "PORTRAIT"}) {
        REQUIRE(parseWindowDisplay(text).has_value());
        const WindowDisplayChoice choice = parseWindowDisplay(text).value_or(WindowDisplayChoice{});
        CHECK(choice.kind == WindowDisplayChoice::Kind::Portrait);
        CHECK(pickDisplay(choice, kDesk) == std::optional<std::size_t>{1});
    }
    // A square or landscape-only desk has none.
    const std::array<DisplayInfo, 2> flat{{
        {"A", {0, 0, 1920, 1080}, {0, 0, 1920, 1040}},
        {"B", {1920, 0, 1200, 1200}, {1920, 0, 1200, 1160}},
    }};
    const WindowDisplayChoice portrait{WindowDisplayChoice::Kind::Portrait, 0, {}};
    CHECK_FALSE(pickDisplay(portrait, flat).has_value());
}

TEST_CASE("window display: other text matches part of a display's name", "[core][window_display]") {
    // Parses `text`, which must be a name, and picks from kDesk.
    const auto pickName = [](const char* text) {
        const WindowDisplayChoice choice = parseWindowDisplay(text).value_or(WindowDisplayChoice{});
        CHECK(choice.kind == WindowDisplayChoice::Kind::Name);
        return pickDisplay(choice, kDesk);
    };
    CHECK(pickName("pnp") == std::optional<std::size_t>{2}); // case does not matter
    CHECK(pickName("LG HDR") == std::optional<std::size_t>{1});
    CHECK_FALSE(pickName("Samsung").has_value());
    CHECK_FALSE(pickName("2x").has_value()); // not all digits: a name
}

TEST_CASE("window display: a window that fits is centred at its own size", "[core][window_display]") {
    // 960 x 720 on the portrait display's usable area.
    CHECK(placeWindow({-1080, 249, 1080, 1872}, 960, 720) == DisplayRect{-1020, 825, 960, 720});
    // Exactly the area's size.
    CHECK(placeWindow({10, 20, 640, 448}, 640, 448) == DisplayRect{10, 20, 640, 448});
}

TEST_CASE("window display: a window too big is scaled down to fit, keeping its shape", "[core][window_display]") {
    // 1920 x 1080 onto 1080 wide: limited by the width, 1080 x 607 (rounded down), centred vertically.
    CHECK(placeWindow({-1080, 249, 1080, 1872}, 1920, 1080) == DisplayRect{-1080, 881, 1080, 607});
    // Limited by the height: 1000 x 2000 into 1080 x 1000 becomes 500 x 1000.
    CHECK(placeWindow({0, 0, 1080, 1000}, 1000, 2000) == DisplayRect{290, 0, 500, 1000});
    // Never below one pixel.
    CHECK(placeWindow({0, 0, 100, 100}, 10000, 1) == DisplayRect{0, 49, 100, 1});
}

TEST_CASE("window display: an empty area keeps the window's size at its corner", "[core][window_display]") {
    CHECK(placeWindow({5, 6, 0, 0}, 960, 720) == DisplayRect{5, 6, 960, 720});
}
