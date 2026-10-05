// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "world_objects/flags.h"

namespace coney {

/// One `HuCreate(name, type, {x, y, z}, heading, unused, player, gang, flag)` a level script made, as the binding read
/// its arguments (docs/references/bindings/character.md#hucreate).
struct HumanCreation {
    std::string name; ///< The human's name (`Rembrandt`), as `HuFind` and the scripts use it.
    int type = 0;     ///< The character type: the `CfgChar` record (32 Rembrandt, 40 Ash).
    /// The position {x, y, z} in metres, game axes with z up; nothing when the argument was not a table of three
    /// numbers (a call of a binding Coney lacks leaves it nil).
    std::optional<std::array<float, 3>> position;
    float headingDegrees = 0.0F; ///< The facing, degrees about the vertical axis.
    int playerIndex = 0;         ///< 0 for an AI human, 1 for player 1, 2 for player 2.
    double handle = 0;           ///< The handle the binding returned.
    /// The character model the type is drawn as (characters::modelNameFor(): `warr_cl` for Cleon); empty when the
    /// type has no `CfgChar` record.
    std::string model;
    /// Where the last `TeleportToFlag` put the human, with no ground snap; nothing while it stands where it was made.
    std::optional<world_objects::Placement> teleported;
    std::uint32_t teleports = 0; ///< Teleports so far: a change tells a running level to move the human.
};

/// The humans the level scripts have created, in the order of the `HuCreate` calls: Coney's stand-in for the original's
/// 60 human slots until the characters exist as game objects. The level flow clears it when a level starts; the play
/// mode takes player 1 from it.
///
/// Research: docs/research/characters.md#creation, docs/research/characters.md#level-starts
class CreatedHumans {
  public:
    /// The most humans the original's slots hold; a creation past it gets no slot (`HuCreate` returns `NilHandle`).
    static constexpr std::size_t kCapacity = 60;

    /// Keeps `human`. Returns false, keeping nothing, when kCapacity humans are already kept.
    bool add(HumanCreation human);
    /// Every human kept, oldest first.
    [[nodiscard]] const std::vector<HumanCreation>& all() const { return m_humans; }
    /// The first human created for player `index` (1 for player 1); null when there is none.
    [[nodiscard]] const HumanCreation* player(int index) const;
    /// The human with `handle`; null when none has it.
    [[nodiscard]] HumanCreation* find(double handle);
    /// Where the human with `handle` stands: where it was last teleported, else where it was made; nothing for a
    /// handle no human has or a human made without a position. The flags' ObjectLocator.
    [[nodiscard]] std::optional<world_objects::Placement> placement(double handle) const;
    /// Forgets every human: a new level starts with none.
    void clear() { m_humans.clear(); }

  private:
    std::vector<HumanCreation> m_humans;
};

} // namespace coney
