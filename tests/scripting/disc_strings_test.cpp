// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the UI strings of every language loaded by running the game's own scripts
// (enum_preload.lua, config_preload2.lua and config_strings_<code>.lua) through Coney's Lua 4.0 VM, with the counts
// per table compared to docs/research/gui.md#coneys-implementation. It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise. It prints counts only, never text (LEGAL.md).

#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/language.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gui/global_strings.h"
#include "scripting/config_strings.h"

using coney::gui::StringTable;

TEST_CASE("every language's UI strings load from the disc's scripts", "[disc][strings]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    if (!wad) {
        return;
    }
    const coney::script::ScriptSource source = coney::script::wadScriptSource(*wad);

    // HUD strings per language, in the order of the Language enum; the other tables are the same size in all five.
    struct Expected {
        coney::Language language;
        std::size_t hud;
    };
    for (const Expected expected : {Expected{coney::Language::English, 388}, Expected{coney::Language::Spanish, 388},
                                    Expected{coney::Language::French, 386}, Expected{coney::Language::Italian, 387},
                                    Expected{coney::Language::German, 388}}) {
        coney::gui::GlobalStrings strings;
        auto report = coney::script::loadGlobalStrings(source, expected.language, strings);
        INFO("language " << std::string(coney::languageCode(expected.language)));
        REQUIRE(report.has_value());
        std::printf("  %s: %zu HUD, %zu crime, %zu tutorial, %zu command, %zu announce strings; %llu instructions, "
                    "%llu binding calls skipped; highest HUD id %u\n",
                    std::string(coney::languageCode(expected.language)).c_str(), strings.size(StringTable::Hud),
                    strings.size(StringTable::Crime), strings.size(StringTable::Tutorial),
                    strings.size(StringTable::Command), strings.size(StringTable::Announce),
                    static_cast<unsigned long long>(report->instructions),
                    static_cast<unsigned long long>(report->skippedCalls),
                    strings.entries(StringTable::Hud).empty() ? 0U : strings.entries(StringTable::Hud).rbegin()->first);
        CHECK(strings.size(StringTable::Hud) == expected.hud);
        CHECK(strings.size(StringTable::Crime) == 15);
        CHECK(strings.size(StringTable::Tutorial) == 27);
        CHECK(strings.size(StringTable::Command) == 8);
        CHECK(strings.size(StringTable::Announce) == 5);
        // The menus' usage line (0x1f) exists in every language.
        CHECK(!strings.get(0x1f).empty());
    }
}
