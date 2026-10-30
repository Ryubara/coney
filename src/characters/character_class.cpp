// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_class.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

namespace coney::characters {

namespace {

// One row of Human_Init's switch: a class, its plain aliases and its variants (0 ends a list).
struct ClassRow {
    int id;
    std::array<int, 1> aliases;
    std::array<int, 4> variants;
};

// The switch, from docs/research/characters.md#type-to-model.
constexpr std::array<ClassRow, 14> kClasses{{
    {1, {2}, {3, 4}},
    {5, {6}, {7, 8, 9, 10}},
    {11, {12}, {13, 14}},
    {15, {16}, {}},
    {18, {19}, {20}},
    {21, {22}, {25}},
    {26, {27}, {28, 29}},
    {30, {31}, {32}},
    {33, {34}, {35, 36, 37}},
    {38, {}, {39, 40}},
    {41, {}, {42}},
    {43, {}, {44}},
    {45, {}, {46, 47, 48}},
    {221, {}, {222}},
}};

// The Warriors' type groups, in Warrior class order (0 Ajax ... 8 Swan): a first and last type and one more (0: none).
struct WarriorGroup {
    int first;
    int last;
    int extra;
};
constexpr std::array<WarriorGroup, 9> kWarriorGroups{{
    {11, 14, 0},
    {1, 4, 189},
    {15, 17, 0},
    {18, 20, 188},
    {21, 25, 0},
    {26, 29, 191},
    {30, 32, 0},
    {33, 37, 190},
    {5, 10, 0},
}};
// Rembrandt's class also takes Ash (38-40), the one Warrior with two ranges.
constexpr int kAshFirst = 38;
constexpr int kAshLast = 40;
constexpr int kRembrandtWarriorClass = 6;
// The power class of the first Warrior (Ajax); the others follow in order.
constexpr int kFirstWarriorPowerClass = 58;
// The power class a Warriors type outside the groups plays with.
constexpr int kOtherWarriorPowerClass = 7;

// The level numbers the original takes for Armies of the Night: 60-69, though only 60-64 are on the disc
// (docs/research/armies.md#what-it-is).
constexpr int kFirstArmiesLevel = 60;
constexpr int kLastArmiesLevel = 69;
// The last level whose humans Human_Init draws with the `_a` model: its own test takes 60-64
// (docs/research/characters.md#type-to-model).
constexpr int kLastArmiesModelLevel = 64;

// One class's own size (docs/research/characters.md#body-scale): alone, and with game state `+0x158` set (co-op).
struct TypeScale {
    int classType;
    float scale;
    float coopScale;
};

// The classes Human_ApplyTypeScale sizes; any other keeps the size it has.
constexpr std::array<TypeScale, 13> kTypeScales{{
    {5, 1.02F, 1.02F},
    {18, 0.97F, 0.97F},
    {21, 0.98F, 0.98F},
    {23, 0.98F, 0.98F},
    {24, 0.98F, 0.98F},
    {30, 0.97F, 0.97F},
    {33, 1.03F, 1.03F},
    {89, 1.16F, 1.1F},
    {119, 1.2F, 1.1F},
    {120, 1.2F, 1.1F},
    {128, 1.175F, 1.1F},
    {135, 1.2F, 1.03F},
    {161, 1.11F, 1.1F},
}};

} // namespace

CharacterClass characterClassOf(int type) {
    for (const ClassRow& row : kClasses) {
        for (const int alias : row.aliases) {
            if (alias != 0 && alias == type) {
                return CharacterClass{.id = row.id, .variant = false};
            }
        }
        for (const int variant : row.variants) {
            if (variant != 0 && variant == type) {
                return CharacterClass{.id = row.id, .variant = true};
            }
        }
    }
    return CharacterClass{.id = type, .variant = false};
}

// Human_Init's choice of the CfgChar record a human is drawn from: a player made as a plain alias takes the class's.
// @orig 0x00218008 Human_Init (unknown)
int modelRecordType(int type, int playerIndex) {
    const CharacterClass characterClass = characterClassOf(type);
    if (playerIndex < 1 || characterClass.variant) {
        return type;
    }
    return characterClass.id;
}

bool isArmiesLevel(int levelNumber) { return levelNumber >= kFirstArmiesLevel && levelNumber <= kLastArmiesLevel; }

// Human_Init's model: the record's model name, its `_a` one in an Armies of the Night level (+0x112 or +0x114).
// Human_Init's other steps are HuCreate's binding's (humanInitSteps(), docs/research/human-init.md).
// @orig 0x00218008 Human_Init (unknown)
std::optional<std::string> modelNameFor(int type, int playerIndex, int levelNumber, const CfgCharModel& modelOf) {
    if (!modelOf) {
        return std::nullopt;
    }
    std::optional<std::string> model = modelOf(modelRecordType(type, playerIndex));
    // Human_Init's own test, 60-64, not GameState_IsArmiesLevel's 60-69.
    if (model && levelNumber >= kFirstArmiesLevel && levelNumber <= kLastArmiesModelLevel) {
        *model += "_a";
    }
    return model;
}

int warriorClassOf(int type) {
    if (type >= kAshFirst && type <= kAshLast) {
        return kRembrandtWarriorClass;
    }
    for (std::size_t index = 0; index < kWarriorGroups.size(); ++index) {
        const WarriorGroup& group = kWarriorGroups.at(index);
        if ((type >= group.first && type <= group.last) || (group.extra != 0 && type == group.extra)) {
            return static_cast<int>(index);
        }
    }
    return kOtherWarriorClass;
}

int warriorPowerClassOf(int type) {
    const int warrior = warriorClassOf(type);
    return warrior == kOtherWarriorClass ? kOtherWarriorPowerClass : kFirstWarriorPowerClass + warrior;
}

// Human_Init's power class byte +0x1b9: a type of the Warriors' category takes his Warrior's, any other the record's.
// @orig 0x00218008 Human_Init (unknown)
int powerClassOf(int type, int category, int ownClass) {
    return category == kWarriorsCategory ? warriorPowerClassOf(type) : ownClass;
}

// Human_ApplyTypeScale's sizes: the listed classes' own, and in co-op the big ones' smaller one.
// @orig 0x00219638 Human_ApplyTypeScale (unknown)
std::optional<float> typeScaleOf(int classType, bool coop) {
    const auto found = std::ranges::find(kTypeScales, classType, &TypeScale::classType);
    if (found == kTypeScales.end()) {
        return std::nullopt;
    }
    return coop ? found->coopScale : found->scale;
}

} // namespace coney::characters
