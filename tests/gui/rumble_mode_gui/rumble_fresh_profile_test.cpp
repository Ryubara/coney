// SPDX-License-Identifier: GPL-3.0-or-later
// What a fresh profile unlocks for the Rumble menu (#1704): the records global.lua's CfgUnlockables unlocks at boot
// with UM_Unlock(0, 0, 0) and UM_Unlock(8, 0, 0), as the original's fresh boot lists them on Choose Gangs.
#include <array>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gui/rumble_mode_gui/rumble_data.h"
#include "gui/rumble_mode_gui/rumble_menu.h"
#include "support/rumble_fixtures.h"
#include "support/rumble_menu_harness.h"
#include "warriors/unlockables.h"

using coney::Unlockables;
using coney::UnlockKind;
using coney::gui::RumbleScreen;
using coney::test::RumbleMenuHarness;
using coney::test::TestGang;
using Roster = coney::gui::RumbleGangEntry::Roster;

namespace {

// Synthetic gangs with the disc's ids in rumble_gang.lua's order (the names are invented): the four a fresh boot
// unlocks (5, 3, 4, 1) among locked ones. Gang 4's first two types are the stand-in table's 100 and 99.
std::vector<std::byte> discOrderGangChunk() {
    return coney::test::rumbleGangChunk({
        TestGang{5, "GANG FIVE", {89, 90, 91, 92, 93, 94, 91, 92, 93}},
        TestGang{13, "GANG THIRTEEN", {128, 129, 134, 130, 131, 132, 133, 313, 314}},
        TestGang{2, "GANG TWO", {178, 186, 185, 196, 180, 192, 196, 194, 197}},
        TestGang{3, "GANG THREE", {220, 223, 224, 225, 226, 227, 228, 225, 226}},
        TestGang{4, "GANG FOUR", {100, 99, 101, 102, 103, 104, 105, 101, 102}},
        TestGang{18, "GANG EIGHTEEN", {166, 167, 168, 169, 170, 171, 168, 169, 170}},
        TestGang{1, "GANG ONE", {2, 6, 12, 16, 34, 22, 19, 27, 31}},
        TestGang{35, "GANG THIRTY-FIVE", {38, 43, 45, 41, 49, 50, 51, 52, 53}},
    });
}

} // namespace

TEST_CASE("a fresh profile's Choose Gangs lists gangs 5, 3, 4 and 1 in the script's order", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (runtime)
    // Run r-20261128-rumble-fresh-gangs: on a fresh boot the gang list at 0x0063ee4c held ids 5, 3, 4 and 1 at indices
    // 0-3, and gang 4's resolved roster read 101 102 101 102 103 104 105 101 102.
    RumbleMenuHarness h("10 tap cross\n20 tap cross\n");
    h.files["rumble_gang.lua"] = discOrderGangChunk();
    h.to(25);
    REQUIRE(h.menu->screen() == RumbleScreen::ChooseGangs);
    std::vector<std::int32_t> ids;
    ids.reserve(h.data.gangs.size());
    for (const auto& gang : h.data.gangs) {
        ids.push_back(gang.id);
    }
    CHECK(ids == std::vector<std::int32_t>{5, 3, 4, 1});
    REQUIRE(h.data.gangs.size() == 4);
    CHECK(h.data.gangs.at(2).rosters[0] == Roster{101, 102, 101, 102, 103, 104, 105, 101, 102});
    // Gang 1's types are all unlocked at boot, and none is in the stand-in table: its roster stays.
    CHECK(h.data.gangs.at(3).rosters[0] == Roster{2, 6, 12, 16, 34, 22, 19, 27, 31});
}

TEST_CASE("a fresh profile unlocks the character types global.lua unlocks at boot", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (runtime)
    // Run r-20261128-rumble-fresh-gangs: records 216-224 and 270-283 (level 0 and level 8, group 0, item 0, of
    // docs/references/unlockables.md#rumble-character) were unlocked in the locked bits at 0x006fe8f8 after the title.
    const Unlockables fresh = Unlockables::freshProfile();
    constexpr std::array kUnlocked{2,  6,   12,  16,  19,  22,  27,  31,  34,  91,  92, 93,
                                   94, 101, 102, 103, 104, 105, 224, 225, 226, 227, 228};
    for (const int type : kUnlocked) {
        INFO("type " << type);
        CHECK(fresh.isUnlocked(UnlockKind::CharacterType, type));
    }
    // The stand-in table's types of these gangs stay locked, so the menu replaces them.
    for (const int type : {89, 90, 99, 100, 220, 223}) {
        INFO("type " << type);
        CHECK(!fresh.isUnlocked(UnlockKind::CharacterType, type));
    }
}
