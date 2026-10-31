// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/starting_money.h"

#include <algorithm>
#include <cstdint>

namespace coney::characters {

namespace {

constexpr std::string_view kNone = "none";
constexpr std::string_view kGroupPrefix = "grp_";
// The category whose alternate byte picks record kAltRecord, and the categories that draw for the brain.
constexpr int kAltCategory = 4;
constexpr int kAltRecord = 16;
constexpr int kMaxMoney = 999;
constexpr int kPercent = 100;
// The ped types: categories kAltCategory and kShopCategory a plain one, or a 2 on category 4's roll below kTwoRoll, for
// a human with kRichMoney dollars or more and for a woman; kTougherCategoryA and B a 3.
constexpr int kShopCategory = 6;
constexpr int kTougherCategoryA = 5;
constexpr int kTougherCategoryB = 7;
constexpr int kTwoRoll = 10;
constexpr int kRichMoney = 21;
constexpr std::uint16_t kPlainPed = 1;
constexpr std::uint16_t kRichPed = 2;
constexpr std::uint16_t kToughPed = 3;

} // namespace

// Human_Init's record for the money: category 4 with the women's byte reads record 16.
// @orig 0x00218008 Human_Init (unknown)
int moneyCategory(const CarryConfig& config) {
    return config.category == kAltCategory && config.altRecord ? kAltRecord : config.category;
}

StartingCarry rollStartingCarry(const CarryConfig& config, const ClassMoney& money, GameRandom& random) {
    StartingCarry carry;
    carry.object = std::string(kNone);
    // 1-2. The object: none at or below the no-object chance; else the type's object at or below its drop chance.
    const int roll = random.range(RandomStream::Gameplay, 0, kPercent);
    if (roll > money.noObject) {
        carry.dropChance = config.dropChance;
        const bool group = config.object.find(kGroupPrefix) != std::string::npos;
        if (!group && roll <= config.dropChance) {
            carry.object = config.object;
        }
    }
    // 3. The money: the class range with no cap or with an object; else capped by the drop chance, or none.
    const bool hasObject = carry.object != kNone;
    int dollars = 0;
    bool rolled = true;
    if (config.dropChance == -1 || hasObject) {
        dollars = random.range(RandomStream::Gameplay, money.min, money.max);
    } else if (roll <= money.noObject) {
        dollars = money.min < config.dropChance ? random.range(RandomStream::Gameplay, money.min, config.dropChance)
                                                : config.dropChance;
    } else {
        rolled = false;
    }
    // 4. The bonus.
    if (rolled) {
        if (random.range(RandomStream::Gameplay, 0, kPercent) <= money.bonusChance) {
            dollars = static_cast<int>(static_cast<float>(dollars) * money.bonus);
        }
        carry.money = std::clamp(dollars, 0, kMaxMoney);
    }
    // 5. The brain's ped type by category: category 4's own roll, then the money and the women's byte.
    if (config.category == kAltCategory || config.category == kShopCategory) {
        std::uint16_t ped = kPlainPed;
        if (config.category == kAltCategory && random.range(RandomStream::Gameplay, 0, kPercent) < kTwoRoll) {
            ped = kRichPed;
        }
        carry.pedType = carry.money < kRichMoney && !config.altRecord ? ped : kRichPed;
    } else if (config.category == kTougherCategoryA || config.category == kTougherCategoryB) {
        carry.pedType = kToughPed;
    }
    return carry;
}

} // namespace coney::characters
