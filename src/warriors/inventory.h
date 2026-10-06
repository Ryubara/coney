// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace coney {

/// The inventory item ids the code uses by number (docs/references/inventory.md).
namespace item {
inline constexpr int kRevive = 1;      ///< A flash: a revive.
inline constexpr int kMoney = 2;       ///< Money, in dollars.
inline constexpr int kSprayPaint = 3;  ///< Spray-paint charges.
inline constexpr int kHandcuffs = 5;   ///< Handcuffs.
inline constexpr int kHandcuffKey = 6; ///< Handcuff (skeleton) keys.
inline constexpr int kStolenLoot = 10; ///< Loot from a store.
inline constexpr int kCarStereo = 11;  ///< A car stereo.
} // namespace item

/// One item slot of a player's inventory: the 0x2c-byte entry at block `+0x04 + id × 0x2c`.
struct InventoryItem {
    std::string objectName; ///< `+0x00`: the object `CfgInventoryItem` makes the item (31 characters).
    int count = 0;          ///< `+0x20`: how many the player has.
    std::string sound;      ///< `+0x24`: the pickup sound (the original keeps its name hash; Coney the name).
    int durationMs = 0;     ///< `+0x28`: a duration in ms; what it does is not traced.
};

/// The two players' inventories (`W_GameState + 0x480`, two blocks of `0x3f4` bytes): 23 item slots each, ids 0-22,
/// with money (item 2), revives (1), spray paint (3) and handcuff keys (6) among them. Players are 0 and 1 here; the
/// bindings' 1 and 2 are converted by the caller. Out-of-range players and items read 0 and change nothing, as
/// `Inventory_Count` (`0x0041e420`) answers.
///
/// **Limits.** Every count is clamped to the item's limits (the table at `0x0058b300`), whose values the research does
/// not give, except the revives': at most 3, or 4 with the upgrade (type 6, data 7), `0x0041df10`. **Coney's
/// stand-in:** every other item is kept at 0 or above with no upper bound (open item on
/// docs/research/player-state.md).
///
/// Research: docs/research/player-state.md#inventory, docs/references/inventory.md
class Inventory {
  public:
    /// Players.
    static constexpr int kPlayers = 2;
    /// Item slots per player.
    static constexpr int kItems = 23;
    /// The revives carried without the upgrade, and with it (`0x0041df10`).
    static constexpr int kRevives = 3;
    static constexpr int kRevivesUpgraded = 4;
    /// The upper bound of an item whose limit Coney does not know.
    static constexpr int kNoLimit = std::numeric_limits<int>::max();

    /// `CfgInventoryItem(object, id, count, sound, ms)`: gives slot `id` its object, count, pickup sound and duration
    /// in both players' inventories. An id outside 0-22 does nothing.
    /// @orig 0x0041e250 Inventory_SetItem (unknown)
    void configure(int id, const std::string& objectName, int count, const std::string& sound, int durationMs);

    /// How many of item `id` player `player` has; 0 outside players 0-1 and items 0-22.
    /// @orig 0x0041e420 Inventory_Count (unknown)
    [[nodiscard]] int count(int player, int id) const;
    /// Whether player `player` has at least one of item `id`.
    /// @orig 0x0041ed88 InvPlayerHasItem (unknown)
    [[nodiscard]] bool has(int player, int id) const { return count(player, id) > 0; }

    /// Adds `amount` (negative takes) of item `id` to player `player`, clamped to the item's limits. Returns the change
    /// actually made (0 for a bad player or item).
    /// @orig 0x0041ef98 InvGiveItem (unknown)
    int give(int player, int id, int amount);
    /// Sets player `player`'s count of item `id`, clamped to the item's limits.
    void set(int player, int id, int value);

    /// Sets player `player`'s money (item 2), clamped, and tells the HUD to stop counting toward the old value
    /// (moneySets(): the original clears the player object's `+0xe20`, inferred to be the money display).
    /// @orig 0x0041eeb8 InvSetMoney (unknown)
    void setMoney(int player, int value);
    /// How many times setMoney() has run for `player`: a HUD that sees it change shows the new amount at once.
    [[nodiscard]] std::uint32_t moneySets(int player) const;

    /// Sets player `player`'s spray-paint charges (item 3), clamped. The first positive amount also raises the
    /// spray-paint hint flag (sprayHintPending()), a game-state flag that later triggers a one-off HUD hint.
    /// @orig 0x0041f030 InvSetSpraycanCharges (unknown)
    void setSprayPaint(int player, int value);
    /// Whether the spray-paint hint is due: set by the first positive setSprayPaint(); the HUD clears it.
    [[nodiscard]] bool sprayHintPending() const { return m_sprayHintPending; }
    /// The HUD has shown the spray-paint hint.
    void clearSprayHint() { m_sprayHintPending = false; }

    /// Whether the revive upgrade (unlockable type 6, data 7) is held: the revive limit is then 4.
    void setReviveUpgrade(bool on) { m_reviveUpgrade = on; }
    /// The upper limit of item `id`: kRevives (or kRevivesUpgraded) for revives, kNoLimit for the rest.
    [[nodiscard]] int limit(int id) const;

    /// Item slot `id` of player `player`; null outside the ranges.
    [[nodiscard]] const InventoryItem* slot(int player, int id) const;

    /// Empties both players' counts, keeping each slot's configuration.
    void clearCounts();

  private:
    // Whether `player` and `id` name a slot.
    [[nodiscard]] static bool valid(int player, int id);
    // The count clamped to item `id`'s limits.
    [[nodiscard]] int clamp(int id, long long value) const;

    std::array<std::array<InventoryItem, kItems>, kPlayers> m_items{};
    std::array<std::uint32_t, kPlayers> m_moneySets{};
    bool m_sprayHintPending = false;
    bool m_sprayHintGiven = false; // the first positive spray-paint set has happened
    bool m_reviveUpgrade = false;
};

} // namespace coney
