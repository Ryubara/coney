// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace coney::characters {

/// A character type's behaviour class, as `Human_Init` remaps `HuCreate`'s type with a fixed switch: a type in the
/// switch is either a plain alias of its class or a variant of it (which sets a flag); a type not in it is its own
/// class (docs/research/characters.md#type-to-model).
struct CharacterClass {
    int id = 0;           ///< The class (`+0xcc`): 30 for types 30, 31 and 32.
    bool variant = false; ///< The type is a variant of the class (32 of 30), not a plain alias (31 of 30).
};

/// The class of character type `type`.
/// @orig 0x00218008 Human_Init (unknown)
[[nodiscard]] CharacterClass characterClassOf(int type);

/// The type whose `CfgChar` record names the model a human of `type` is drawn as: the type's own record when the human
/// is not a player (`playerIndex` below 1) or the type is a variant; otherwise, for a player made as a plain alias, the
/// class's record. So a player made as type 2 is drawn as type 1's model, while type 32 keeps its own.
[[nodiscard]] int modelRecordType(int type, int playerIndex);

/// Whether level number `levelNumber` is one of the Armies of the Night bonus levels (60-64), which draw every
/// character with its `<model>_a` model (`CfgChar`'s `+0x114`).
[[nodiscard]] bool isArmiesLevel(int levelNumber);

/// The model name argument of type `type`'s `CfgChar` call (`warr_re_cv` for 32); nothing when the type has no call.
using CfgCharModel = std::function<std::optional<std::string>(int type)>;

/// The model name a human of `type`, made for player `playerIndex` in level number `levelNumber`, is drawn as: the
/// model-name argument of modelRecordType()'s `CfgChar` call, with `_a` appended in levels 60-64; nothing when that
/// type has no `CfgChar` call (`modelOf` answers). The caller looks the name up in the Character List, which the
/// original did once when `CfgChar` ran (`+0x112`, `+0x114`; `0xffff` when absent).
[[nodiscard]] std::optional<std::string> modelNameFor(int type, int playerIndex, int levelNumber,
                                                      const CfgCharModel& modelOf);

/// The `CfgChar` category (`+0x11b`) of the Warriors, whose players take their Warrior's power class.
inline constexpr int kWarriorsCategory = 14;
/// The Warrior class of every type that is not one of the nine Warriors.
inline constexpr int kOtherWarriorClass = 9;

/// The Warrior class (human `+0x1ba`, the `CfgWarriorClass` record) of character type `type`: 0 Ajax (types 11-14),
/// 1 Cleon (1-4, 189), 2 Cochise (15-17), 3 Cowboy (18-20, 188), 4 Fox (21-25), 5 Vermin (26-29, 191), 6 Rembrandt
/// and Ash (30-32, 38-40), 7 Snow (33-37, 190), 8 Swan (5-10), kOtherWarriorClass for any other
/// (docs/research/characters.md#power-classes). **Coney's name**: the research gives the function no name.
/// @orig 0x00222c20 Human_WarriorClassOfType (unknown)
[[nodiscard]] int warriorClassOf(int type);

/// The power class a player of a Warriors type plays with (`+0x1b9`): 58-66 by Warrior in warriorClassOf()'s order
/// (58 Ajax ... 64 Rembrandt and Ash ... 66 Swan), 7 for a type that is none of them. **Coney's name**, as above.
/// @orig 0x00222ba8 Human_WarriorPowerClassOfType (unknown)
[[nodiscard]] int warriorPowerClassOf(int type);

/// The power class a human of `type` plays with, as `Human_Init` sets `+0x1b8` and `+0x1b9`: the type's own `CfgChar`
/// byte `+0x11d` (`ownClass`), except that a player (`player`) of category kWarriorsCategory takes
/// warriorPowerClassOf().
[[nodiscard]] int powerClassOf(int type, int category, int ownClass, bool player);

/// The index of `CfgChar`'s model-name argument (0-based): the tenth argument, after the type, four bytes, the health,
/// the damage and attack tables and the damage scale (docs/references/characters.md#fields).
inline constexpr std::size_t kCfgCharModelArgument = 9;

} // namespace coney::characters
