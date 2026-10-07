// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "combat/power_class.h"
#include "scripting/lua_value.h"
#include "scripting/script_bindings.h"

// The character types the configuration scripts define: one `CfgChar` call each, read from the calls the scripts made
// (script::RecordedCalls), kept as the arguments give them. The AI fighters' configuration and the debug menus' change
// of character read them here.
// Research: docs/research/characters.md#classes, docs/references/bindings/config.md#cfgchar

namespace coney::characters {

/// One character type as its `CfgChar` call configures it (the record at `0x00684620 + type × 0x1ac`). Every field
/// but the type may be missing from a call: a missing number is nothing, a missing table empty.
struct CharacterType {
    int type = 0;                  ///< The type id `HuCreate` takes.
    std::optional<int> behaviour;  ///< Byte `+0x11a`: the brain type of an AI human of the class.
    std::optional<int> category;   ///< Byte `+0x11b`: the category (kWarriorsCategory for the Warriors).
    std::optional<int> speedClass; ///< Byte `+0x11c`: the speed class (a fallback the original does not use in play).
    std::optional<int> powerClass; ///< Byte `+0x11d`: the power class (`CfgPowerClass`) the type fights with.
    std::optional<int> health;     ///< `+0x116`: the class's health.
    std::vector<std::int16_t> damage;  ///< `+0xb8`: the damage table, times the call's damage scale, truncated.
    std::vector<std::uint8_t> attacks; ///< `+0x11e`: the attack table (an AI's attack weights), each clamped to a byte.
    std::string model;                 ///< The model name (`warr_re_cv`); empty when the call gave none.
    std::optional<int> warrior;        ///< `+0x118`: the Warrior index of a playable character (inferred).
    /// `+0x14b`, `CfgChar`'s 13th argument: 1 for the women's types, whose vocal animation sounds use the `_female`
    /// entries (docs/research/sound-events.md#players).
    std::optional<int> female;
};

/// The type `call` (a recorded `CfgChar` call's arguments) configures; nothing when its first argument is not a number.
[[nodiscard]] std::optional<CharacterType> parseCfgChar(std::span<const script::Value> call);

/// The power class `call` (a recorded `CfgPowerClass` call's arguments) configures, over `base`: the fields a call
/// leaves out keep `base`'s (docs/references/bindings/config.md#cfgpowerclass).
[[nodiscard]] combat::PowerClass parseCfgPowerClass(std::span<const script::Value> call,
                                                    const combat::PowerClass& base);

/// What a player of one character type takes from the configuration when he is made (`Human_Init`,
/// `Human_AttachInstance`; docs/research/characters.md#power-classes, docs/research/combat.md#damage-table).
struct PlayerTraits {
    int classType = 0;                ///< The character class (characterClassOf()) whose record gives the damage.
    std::vector<std::int16_t> damage; ///< The class record's damage table (`+0xb8`); empty when it has none.
    int powerClassId = 0;             ///< The power class he plays with (`+0x1b9`, powerClassOf()).
    std::optional<combat::PowerClass> powerClass; ///< Its `CfgPowerClass` record; nothing when none was recorded.
    int warriorClass = 0;                         ///< His Warrior class (`+0x1ba`, warriorClassOf()).
    int damagePercent = 0; ///< That class's damage scale (`CfgWarriorClass` byte `+0x06`); 0 when none was recorded.
};

/// Every character type the configuration defines, by type: the last `CfgChar` call of each type wins, as the
/// original's record keeps the last write. It also keeps the power and Warrior classes a player of a type reads.
class CharacterTypes {
  public:
    /// The types of the `CfgChar` calls in `recorded`, with its `CfgPowerClass` and `CfgWarriorClass` calls.
    [[nodiscard]] static CharacterTypes fromRecorded(const script::RecordedCalls& recorded);

    /// What a player made as `type` takes from the configuration; nothing when the type has no `CfgChar` call.
    /// **Coney choice**: a missing power class or Warrior class leaves the player's defaults (class 64's runtime
    /// values, unscaled damage), where the original would read a zeroed record.
    [[nodiscard]] std::optional<PlayerTraits> playerTraitsOf(int type) const;

    /// Every type, in rising order.
    [[nodiscard]] const std::vector<CharacterType>& all() const { return m_types; }
    /// Type `type`, or null when the configuration has none.
    [[nodiscard]] const CharacterType* find(int type) const;
    /// Whether no type is defined (no configuration was recorded).
    [[nodiscard]] bool empty() const { return m_types.empty(); }

    /// The model a human of `type` made for player `playerIndex` in level number `levelNumber` is drawn as
    /// (modelNameFor(): a player of a plain alias takes its class's model, levels 60-64 the `_a` one); nothing when
    /// that record is missing or names no model.
    [[nodiscard]] std::optional<std::string> modelFor(int type, int playerIndex, int levelNumber) const;

  private:
    std::vector<CharacterType> m_types; // sorted by type, one each
    // The last `CfgPowerClass` call of each class id, and the damage % (`CfgWarriorClass` `+0x06`) of each Warrior
    // class.
    std::vector<std::pair<int, combat::PowerClass>> m_powerClasses;
    std::vector<std::pair<int, int>> m_damagePercents;
};

} // namespace coney::characters
