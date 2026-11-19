// SPDX-License-Identifier: GPL-3.0-or-later
// The settings the console reads and changes (docs/guides/console.md#settings): each kind checks its value, applies it
// at once and says how it now reads.
#include "platform/game_settings.h"

#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

using coney::platform::boolSetting;
using coney::platform::choiceSetting;
using coney::platform::numberSetting;
using coney::platform::wholeSetting;

TEST_CASE("an on or off setting takes on and off and nothing else") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (set)
    bool value = true;
    auto setting = boolSetting("vsync", "Waits", [&value] { return value; }, [&value](bool on) { value = on; });
    CHECK(setting.name == "vsync");
    CHECK(setting.get() == "on");
    CHECK(setting.set("off").value() == "off");
    CHECK_FALSE(value);
    const auto refused = setting.set("maybe");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error() == "vsync takes on or off, not \"maybe\"");
    CHECK_FALSE(value);
}

TEST_CASE("a whole number setting takes a number in its range and says how it now reads") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (set)
    int value = 60;
    auto setting =
        wholeSetting("fps_cap", "Frames", 0, 1000, [&value] { return value; }, [&value](int n) { value = n; });
    CHECK(setting.get() == "60");
    CHECK(setting.set("144").value() == "144");
    CHECK(value == 144);
    CHECK(setting.set("1001").error() == "fps_cap takes a number from 0 to 1000, not 1001");
    CHECK(setting.set("-1").error() == "fps_cap takes a number from 0 to 1000, not -1");
    CHECK(setting.set("fast").error() == "fps_cap takes a whole number, not \"fast\"");
    CHECK(setting.set("").error() == "fps_cap takes a whole number, not \"\"");
    CHECK(value == 144);
}

TEST_CASE("a number setting takes a decimal in its range") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (set)
    double value = 1.0;
    auto setting =
        numberSetting("volume", "Loudness", 0.0, 1.0, [&value] { return value; }, [&value](double v) { value = v; });
    CHECK(setting.get() == "1");
    CHECK(setting.set("0.25").value() == "0.25");
    CHECK(value == 0.25);
    CHECK(setting.set("1.5").error() == "volume takes a number from 0 to 1, not 1.5");
    CHECK(setting.set("loud").error() == "volume takes a number, not \"loud\"");
    CHECK(value == 0.25);
}

TEST_CASE("a choice setting takes one of its choices") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (set)
    std::string value = "ps2";
    auto setting = choiceSetting(
        "display_preset", "The picture", {"ps2", "xbox", "high"}, [&value] { return value; },
        [&value](std::string_view choice) { value = std::string(choice); });
    CHECK(setting.get() == "ps2");
    CHECK(setting.set("xbox").value() == "xbox");
    CHECK(value == "xbox");
    CHECK(setting.set("ultra").error() == "display_preset takes one of ps2, xbox, high, not \"ultra\"");
    CHECK(value == "xbox");
}
