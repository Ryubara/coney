// SPDX-License-Identifier: GPL-3.0-or-later
// What a human whose health ran out drops (human::dropCarried(), Human_DropCarried 0x00232c60): synthetic humans, the
// game's random numbers' stand-in generator, whose draws a copy of it predicts.
#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_random.h"
#include "human/carried_drop.h"

namespace {

using coney::GameRandom;
using coney::RandomStream;
using coney::human::CarriedDrop;
using coney::human::dropCarried;
using coney::human::DropInput;

// The brain types the drop tells apart (ai::BrainType's numbers).
constexpr int kCop = 1;
constexpr int kGang = 2;
constexpr int kCivilian = 4;
constexpr int kDealer = 5;
// Inventory items: money and the removal time of each kind of drop.
constexpr int kMoneyItem = 2;
constexpr int kCuffsItem = 5;
constexpr int kKeyItem = 6;
constexpr int kOtherItem = 10;

// No sector is blocked.
bool noneBlocked(int /*k*/) { return false; }

// An AI gang soldier with `money` dollars, no pocket, no cuffs and no object.
DropInput soldier(int money) {
    DropInput input;
    input.brainType = kGang;
    input.money = money;
    input.carried = "none";
    return input;
}

} // namespace

TEST_CASE("a soldier knocked out with $5 leaves a $5 money pickup at his feet and keeps none", "[human][combat]") {
    // Expected: crimes.md#down-drop, confirmed (code) at 0x00232c60; confirmed (runtime), run
    // r-20261119-down-drop-money (a yard AI of brain type 2 with $5 left a dyn_money holding 5 and had 0).
    GameRandom random;
    const GameRandom before = random;
    const CarriedDrop drop = dropCarried(soldier(5), random, noneBlocked);
    REQUIRE(drop.objects.size() == 1);
    CHECK(drop.objects[0].type == "dyn_money");
    CHECK(drop.objects[0].money == 5);
    CHECK(drop.objects[0].removalItem == kMoneyItem);
    CHECK_FALSE(drop.objects[0].sector.has_value());
    CHECK(drop.money == 0);
    // An AI's whole money takes no draw.
    CHECK(random.index(RandomStream::Gameplay) == before.index(RandomStream::Gameplay));
}

TEST_CASE("a human with no money and nothing carried drops nothing", "[human][combat]") {
    // Expected: crimes.md#down-drop, confirmed (code) at 0x00232c60 (an amount below $1 makes no object).
    GameRandom random;
    const CarriedDrop drop = dropCarried(soldier(0), random, noneBlocked);
    CHECK(drop.objects.empty());
    CHECK(drop.money == 0);
}

TEST_CASE("a human without a brain drops nothing and keeps his money", "[human][combat]") {
    // Expected: crimes.md#down-drop, confirmed (code) at 0x00232c60 (step 1).
    DropInput input = soldier(5);
    input.brainType.reset();
    GameRandom random;
    const CarriedDrop drop = dropCarried(input, random, noneBlocked);
    CHECK(drop.objects.empty());
    CHECK(drop.money == 5);
}

TEST_CASE("a civilian drops his class's share of his money, at least $1", "[human][combat]") {
    // Expected: crimes.md#down-drop, confirmed (code) at 0x00232c60 (brain type 4: money × record +0x0c, at least 1);
    // the shares of crimes.md#starting-money's table (0.1 for category 4).
    DropInput input = soldier(15);
    input.brainType = kCivilian;
    input.dropClass.moneyShare = 0.1F;
    GameRandom random;
    CarriedDrop drop = dropCarried(input, random, noneBlocked);
    REQUIRE(drop.objects.size() == 1);
    CHECK(drop.objects[0].money == 1);
    CHECK(drop.money == 0);
    input.money = 5;
    drop = dropCarried(input, random, noneBlocked);
    REQUIRE(drop.objects.size() == 1);
    CHECK(drop.objects[0].money == 1);
    input.money = 40;
    input.dropClass.moneyShare = 0.25F;
    drop = dropCarried(input, random, noneBlocked);
    REQUIRE(drop.objects.size() == 1);
    CHECK(drop.objects[0].money == 10);
}

TEST_CASE("a carried object is dropped and the human carries none, keeping his money", "[human][combat]") {
    // Expected: crimes.md#down-drop, confirmed (code) at 0x00232c60 (step 4: an AI's object unless +0x278 is -1, the
    // removal time of item 10 for an object other than dyn_revival, dyn_keys or dyn_cuffs; no money after it).
    DropInput input = soldier(5);
    input.carried = "dyn_bat";
    input.carriedChance = 100;
    GameRandom random;
    CarriedDrop drop = dropCarried(input, random, noneBlocked);
    REQUIRE(drop.objects.size() == 1);
    CHECK(drop.objects[0].type == "dyn_bat");
    CHECK(drop.objects[0].removalItem == kOtherItem);
    CHECK(drop.carried == "none");
    CHECK(drop.money == 5);
    // Cuffs carried are timed as item 5.
    input.carried = "dyn_cuffs";
    drop = dropCarried(input, random, noneBlocked);
    REQUIRE(drop.objects.size() == 1);
    CHECK(drop.objects[0].removalItem == kCuffsItem);
    // A chance of -1 keeps the object, and nothing else drops.
    input.carriedChance = -1;
    drop = dropCarried(input, random, noneBlocked);
    CHECK(drop.objects.empty());
    CHECK(drop.carried == "dyn_cuffs");
    CHECK(drop.money == 5);
}

TEST_CASE("a pocket's items drop at his feet and end the drop", "[human][combat]") {
    // Expected: crimes.md#down-drop, confirmed (code) at 0x00232c60 (step 2: +0x254 objects of the pocket item's
    // object, no removal time; nothing more unless a dealer or of ped type 5).
    DropInput input = soldier(5);
    input.pocketItem = 3;
    input.pocketCount = 2;
    input.pocketObject = "dyn_spraycan";
    GameRandom random;
    const CarriedDrop drop = dropCarried(input, random, noneBlocked);
    REQUIRE(drop.objects.size() == 2);
    for (const auto& object : drop.objects) {
        CHECK(object.type == "dyn_spraycan");
        CHECK(object.removalItem == 0);
        CHECK_FALSE(object.sector.has_value());
    }
    CHECK(drop.money == 5);
}

TEST_CASE("a dealer's pocket item goes into the first free sector round him, then his money drops", "[human][combat]") {
    // Expected: crimes.md#down-drop, confirmed (code) at 0x00232c60 (brain type 5: one object, its second argument,
    // 1 m out in the first sector that is not blocked; then the money).
    DropInput input = soldier(7);
    input.brainType = kDealer;
    input.pocketItem = 3;
    input.pocketCount = 4;
    input.pocketObject = "dyn_spraycan";
    std::vector<int> asked;
    const auto blocked = [&asked](int k) {
        asked.push_back(k);
        return k < 2;
    };
    GameRandom random;
    const CarriedDrop drop = dropCarried(input, random, blocked);
    REQUIRE(drop.objects.size() == 2);
    CHECK(drop.objects[0].sector == 2);
    CHECK(asked == std::vector<int>{0, 1, 2});
    CHECK(drop.objects[1].type == "dyn_money");
    CHECK(drop.objects[1].money == 7);
}

TEST_CASE("a cop of a keyless class drops a key and keeps his money", "[human][combat]") {
    // Expected: crimes.md#down-drop, confirmed (code) at 0x00232c60 (brain type 1, class +0xb4 = 0 and unlock (6, 13):
    // dyn_key with item 6's removal time; the cuff draw comes first for his one cuff).
    DropInput input = soldier(5);
    input.brainType = kCop;
    input.cuffs = 1;
    input.dropClass.carriedChance = 0;
    input.keysUnlocked = true;
    GameRandom random;
    const GameRandom before = random;
    const CarriedDrop drop = dropCarried(input, random, noneBlocked);
    REQUIRE(drop.objects.size() == 1);
    CHECK(drop.objects[0].type == "dyn_key");
    CHECK(drop.objects[0].removalItem == kKeyItem);
    CHECK(drop.money == 5);
    CHECK(random.index(RandomStream::Gameplay) == before.index(RandomStream::Gameplay) + 1);
}

TEST_CASE("a cuff holder drops power cuffs on a draw below 51 in the game mode", "[human][combat]") {
    // Expected: crimes.md#down-drop, confirmed (code) at 0x00232c60 (step 3: Random_Int(100) below 51 and game state
    // +0x158 set or unlock (6, 14): dyn_powercuffs with item 5's removal time, then nothing else).
    DropInput input = soldier(5);
    input.cuffs = 1;
    input.gameMode = true;
    // Several draws: each predicted from a copy of the generator.
    GameRandom random;
    for (int run = 0; run < 16; ++run) {
        GameRandom copy = random;
        const bool cuffs = copy.range(RandomStream::Gameplay, 0, 100) < 51;
        const CarriedDrop drop = dropCarried(input, random, noneBlocked);
        REQUIRE(drop.objects.size() == 1);
        CHECK(drop.objects[0].type == (cuffs ? "dyn_powercuffs" : "dyn_money"));
        CHECK(drop.objects[0].removalItem == (cuffs ? kCuffsItem : kMoneyItem));
        CHECK(drop.money == (cuffs ? 5 : 0));
    }
}

TEST_CASE("a player drops his class's range of money from his inventory", "[human][combat]") {
    // Expected: crimes.md#down-drop, confirmed (code) at 0x00232c60 (a player: rand(min, max) of his class's record,
    // at most his inventory's money, taken from it).
    DropInput input = soldier(0);
    input.player = true;
    input.dropClass.moneyMin = 5;
    input.dropClass.moneyMax = 20;
    input.playerMoney = 8;
    GameRandom random;
    GameRandom copy = random;
    const int rolled = copy.range(RandomStream::Gameplay, 5, 20);
    const int expected = rolled < 8 ? rolled : 8;
    const CarriedDrop drop = dropCarried(input, random, noneBlocked);
    REQUIRE(drop.objects.size() == 1);
    CHECK(drop.objects[0].money == static_cast<unsigned>(expected));
    CHECK(drop.playerMoneyTaken == expected);
}
