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

/// The index of `CfgChar`'s model-name argument (0-based): the tenth argument, after the type, four bytes, the health,
/// the damage and attack tables and the damage scale (docs/references/characters.md#fields).
inline constexpr std::size_t kCfgCharModelArgument = 9;

} // namespace coney::characters
