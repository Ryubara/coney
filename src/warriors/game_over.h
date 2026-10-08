// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>

// The engine's game-over check and the hand-off to the mission-failed screen: when every player is knocked out or
// cuffed and his crew cannot help, the mission fails, and 180 updates later the mission-failed mode (0xc) opens.
// Research: docs/research/combat.md#defeat, docs/references/bindings/level.md#enablegameovercheck

namespace coney {

/// One player as the check sees him (the human's state word and inventory).
struct GameOverPlayer {
    bool dead = false;        ///< State `0x100000000`.
    bool knockedOut = false;  ///< State `0x40000` (`Human_IsKnockedOut`).
    bool cuffed = false;      ///< State `0x20000`.
    bool canFreeSelf = false; ///< Player 1 cuffed with upgrade (6, 15) and a handcuff key: spared.
    bool holdsFlash = false;  ///< At least one flash (item 1).
    bool waitBlocked = false; ///< Game state `+0x414 + player` set: no waiting for help.
};

/// Why the mission failed: the `GSTRING.HUD` id of the mission-failed screen's title (`MissionFailed_SetReason`).
enum class GameOverReason : std::uint8_t {
    Beaten = 20, ///< Player 1 is not cuffed (`+0x118` = 0).
    Busted = 21, ///< Player 1 is cuffed (`+0x118` = 1).
};

/// The game-over check (`W_GameState` `+0x155` on, `+0x14c` the level end, `+0x56e6` the route checks).
class GameOverCheck {
  public:
    /// Updates between two route checks while the failure waits for help.
    static constexpr int kRouteCheckUpdates = 37;
    /// The route check's failures in a row that fail the mission.
    static constexpr int kRouteCheckFailures = 4;
    /// Updates from the failure to the mission-failed mode; the countdown a newly pressed cross cuts it to.
    static constexpr int kHandOffUpdates = 180;
    static constexpr int kHandOffCut = 10;

    /// The level's start (`0x00418c68`): the check on, no failure pending.
    void reset();
    /// `EnableGameOverCheck(on)`.
    /// @orig 0x0041d890 GameState_EnableGameOverCheck (unknown)
    void setEnabled(bool on) { m_enabled = on; }
    [[nodiscard]] bool enabled() const { return m_enabled; }

    /// One game-state update of the check, with the players (player 1 first), whether no one else of player 1's gang
    /// is free to help (`Gang_NoneAbleToHelp`) and, asked every kRouteCheckUpdates updates while waiting, whether a
    /// free member has a route to him (`Gang_CanReachToHelp`). Returns the reason when the mission fails now; the
    /// failure also starts the hand-off countdown. Nothing runs while off or once failed.
    /// @orig 0x004197a8 GameState_CheckGameOver (unknown)
    std::optional<GameOverReason> update(std::span<const GameOverPlayer> players, bool noneAbleToHelp,
                                         const std::function<bool()>& canReachToHelp);

    /// Whether the mission has failed (`+0x14c` = 1).
    [[nodiscard]] bool failed() const { return m_reason.has_value(); }
    /// The failure's reason, if failed.
    [[nodiscard]] std::optional<GameOverReason> reason() const { return m_reason; }

    /// One update of the hand-off (`Gm_Level_Update`) after a failure: the countdown runs down by one, or to
    /// kHandOffCut on `crossPressed` (a newly pressed pad bit `0x40`). Returns true on the update it reaches 0, when
    /// the mission-failed mode is to open (once).
    /// @orig 0x00158728 Mode1::Update (unknown)
    bool stepHandOff(bool crossPressed);
    /// The hand-off's countdown, updates left.
    [[nodiscard]] int handOffLeft() const { return m_handOff; }

  private:
    // Fails the mission with player 1's reason.
    GameOverReason fail(const GameOverPlayer& first);

    bool m_enabled = true;
    std::optional<GameOverReason> m_reason;
    int m_waitUpdates = 0; // updates since the last route check while waiting
    int m_routeNoes = 0;   // `+0x56e6`: route checks failed in a row
    int m_handOff = kHandOffUpdates;
    bool m_handedOff = false;
};

} // namespace coney
