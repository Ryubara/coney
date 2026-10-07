// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/starting_money.h"

#include <algorithm>

namespace coney::characters {

namespace {

constexpr std::string_view kNone = "none";
constexpr std::string_view kGroupPrefix = "grp_";
// The category whose alternate byte picks record kAltRecord, and the categories that draw for the brain.
constexpr int kAltCategory = 4;
constexpr int kAltRecord = 16;
constexpr int kMaxMoney = 999;
constexpr int kPercent = 100;

} // namespace

int moneyCategory(const CarryConfig& config) {
    return config.category == kAltCategory && config.altRecord ? kAltRecord : config.category;
}

StartingCarry rollStartingCarry(const CarryConfig& config, const ClassMoney& money, GameRandom& random) {
    StartingCarry carry;
    carry.object = std::string(kNone);
    // 1-2. The object: none at or below the no-object chance; else the type's object at or below its drop chance.
    const int roll = random.range(0, kPercent);
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
        dollars = random.range(money.min, money.max);
    } else if (roll <= money.noObject) {
        dollars = money.min < config.dropChance ? random.range(money.min, config.dropChance) : config.dropChance;
    } else {
        rolled = false;
    }
    // 4. The bonus.
    if (rolled) {
        if (random.range(0, kPercent) <= money.bonusChance) {
            dollars = static_cast<int>(static_cast<float>(dollars) * money.bonus);
        }
        carry.money = std::clamp(dollars, 0, kMaxMoney);
    }
    // 5. Category 4's brain draw, kept for the order of the draws.
    if (config.category == kAltCategory) {
        static_cast<void>(random.range(0, kPercent));
    }
    return carry;
}

} // namespace coney::characters
