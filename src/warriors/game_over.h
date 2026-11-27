// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>

// The engine's game-over check and the hand-off to the mission-failed screen: when every player is knocked out or
// cuffed and his crew cannot help, the mission fails, and 180 updates later the mission-failed mode (0xc) opens.
// Research: docs/research/combat-reactions.md#defeat, docs/references/bindings/level.md#enablegameovercheck

namespace coney {

namespace human {
class Human;
} // namespace human

/// One player as the check sees him (the human's state word and inventory).
struct GameOverPlayer {
    bool dead = false;        ///< State `0x100000000`.
    bool knockedOut = false;  ///< State `0x40000` (`Human_IsKnockedOut`).
    bool cuffed = false;      ///< State `0x20000`.
    bool canFreeSelf = false; ///< Player 1 cuffed with upgrade (6, 15) and a handcuff key: spared.
    bool holdsFlash = false;  ///< At least one flash (item 1).
    bool waitBlocked = false; ///< Game state `+0x414 + player` set: no waiting for help.
};

/// Player 1 as the game-over check reads him from his human (`holdsFlash`: he carries a flash): knocked out is
/// `Human_IsKnockedOut` (state `0x40000`, human::Human::knockedOut(): set at his dying clip's end, not at 0 health) and
/// cuffed `0x20000`. **Coney's stand-ins**: none is dead (Coney keeps no dead state), none frees himself (Coney has no
/// uncuffing) and `+0x414` never blocks the wait.
/// Research: docs/research/combat-reactions.md#mission-failing
/// @orig 0x004197a8 GameState_CheckGameOver (unknown)
[[nodiscard]] GameOverPlayer gameOverPlayer(const human::Human& human, bool holdsFlash);

/// Whether the route test asks this member of player 1's gang for a route to him: not cuffed (`Human_IsCuffed`) and
/// not down or dead (`Human_IsDownOrDead`, human::Human::downOrDead(): dying, knocked out, wounded or dead).
/// Research: docs/research/combat-reactions.md#defeat-helpers
/// @orig 0x00169ea0 Gang_CanReachToHelp (unknown)
[[nodiscard]] bool asksForRoute(const human::Human& member);

/// Why the mission failed: the `GSTRING.HUD` id of the mission-failed screen's title (`MissionFailed_SetReason`).
enum class GameOverReason : std::uint8_t {
    Beaten = 20, ///< Player 1 is not cuffed (`+0x118` = 0).
    Busted = 21, ///< Player 1 is cuffed (`+0x118` = 1).
};

/// The game-over check (`W_GameState` `+0x155` on, `+0x14c` the level end, `+0x56e6` the route checks, `+0x230` the
/// game state's update counter).
class GameOverCheck {
  public:
    /// A new check: on, no failure, the update counter at 0 (the game state's constructor).
    GameOverCheck() = default;
    /// The route check runs on the game state's updates whose count is a multiple of this while the failure waits.
    static constexpr int kRouteCheckUpdates = 37;
    /// The route check's failures in a row that fail the mission.
    static constexpr int kRouteCheckFailures = 4;
    /// Updates from the failure to the mission-failed mode; the countdown a newly pressed cross cuts it to.
    static constexpr int kHandOffUpdates = 180;
    static constexpr int kHandOffCut = 10;
    /// The hand-off's countdown cap in an Armies level.
    static constexpr int kArmiesHandOffCap = 90;

    /// The level's start (`0x00418c68`): the check on, no failure pending, no route checks failed, not an Armies level.
    /// The update counter runs on (only the game state's constructor zeroes it): a fresh check that keeps it.
    void reset() { *this = GameOverCheck(m_updates); }
    /// `EnableGameOverCheck(on)`.
    /// @orig 0x0041d890 GameState_EnableGameOverCheck (unknown)
    void setEnabled(bool on) { m_enabled = on; }
    [[nodiscard]] bool enabled() const { return m_enabled; }

    /// One game-state update (`GameState_Tick`, `0x0041a370`: the update counter, then the check), with the players
    /// (player 1 first), whether no one of player 1's gang, player 1 included, is free to help (`Gang_NoneAbleToHelp`
    /// with no exception: ai::Gang::noneAbleToHelp(nullptr); an empty gang is not "none able") and, asked while waiting
    /// on the updates whose count is a multiple of kRouteCheckUpdates, whether a free member has a route to him
    /// (`Gang_CanReachToHelp`). Returns the reason when the mission fails now; the failure also starts the hand-off
    /// countdown. The counter counts every call; nothing else runs while off, once failed or with no players.
    /// @orig 0x004197a8 GameState_CheckGameOver (unknown)
    std::optional<GameOverReason> update(std::span<const GameOverPlayer> players, bool noneAbleToHelp,
                                         const std::function<bool()>& canReachToHelp);

    /// Whether the mission has failed (`+0x14c` = 1).
    [[nodiscard]] bool failed() const { return m_reason.has_value(); }
    /// The failure's reason, if failed.
    [[nodiscard]] std::optional<GameOverReason> reason() const { return m_reason; }

    /// One update of the hand-off (`Gm_Level_Update`) after a failure: in an Armies level the countdown is first
    /// capped at kArmiesHandOffCap; it is cut to kHandOffCut on `crossPressed` (a newly pressed pad bit `0x40`), runs
    /// down by one, and drops to 0 when `fellOut` (game state `+0x152` bit `0x2`: a player fell out of the world).
    /// Returns true on the update it reaches 0, when the failure's mode is to open (once).
    /// @orig 0x00158728 Mode1::Update (unknown)
    bool stepHandOff(bool crossPressed, bool fellOut);
    /// Whether the level is an Armies level (`GameState_IsArmiesLevel`, `0x0041d110`), set at the level's start: its
    /// hand-off is shorter and opens the Armies game over instead of the mission-failed screen.
    void setArmiesLevel(bool on) { m_armies = on; }
    /// Whether setArmiesLevel() said the level is an Armies level.
    [[nodiscard]] bool armiesLevel() const { return m_armies; }
    /// Whether the hand-off hides the HUD at its first update: a story failure does, in its death camera's start
    /// (`Gm_Level_Update`, `0x00158728`); an Armies level skips that camera and keeps its HUD.
    [[nodiscard]] bool handOffHidesHud() const { return !m_armies; }
    /// The hand-off's countdown, updates left.
    [[nodiscard]] int handOffLeft() const { return m_handOff; }

  private:
    // A fresh check whose update counter stands at `updates`.
    explicit GameOverCheck(std::uint32_t updates) : m_updates(updates) {}
    // Fails the mission with player 1's reason.
    GameOverReason fail(const GameOverPlayer& first);

    bool m_enabled = true;
    std::optional<GameOverReason> m_reason;
    std::uint32_t m_updates = 0; // `+0x230`: the game state's updates, counted by update()
    int m_routeNoes = 0;         // `+0x56e6`: route checks failed in a row
    int m_handOff = kHandOffUpdates;
    bool m_handedOff = false;
    bool m_armies = false;
};

} // namespace coney
