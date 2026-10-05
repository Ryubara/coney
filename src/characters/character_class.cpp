// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_class.h"

#include <array>

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

// The Armies of the Night levels.
constexpr int kFirstArmiesLevel = 60;
constexpr int kLastArmiesLevel = 64;

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

int modelRecordType(int type, int playerIndex) {
    const CharacterClass characterClass = characterClassOf(type);
    if (playerIndex < 1 || characterClass.variant) {
        return type;
    }
    return characterClass.id;
}

bool isArmiesLevel(int levelNumber) { return levelNumber >= kFirstArmiesLevel && levelNumber <= kLastArmiesLevel; }

std::optional<std::string> modelNameFor(int type, int playerIndex, int levelNumber, const CfgCharModel& modelOf) {
    if (!modelOf) {
        return std::nullopt;
    }
    std::optional<std::string> model = modelOf(modelRecordType(type, playerIndex));
    if (model && isArmiesLevel(levelNumber)) {
        *model += "_a";
    }
    return model;
}

} // namespace coney::characters
