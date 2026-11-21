// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "movies/movie_mode.h"
#include "net/protocol.h"

// The running game's side of a network session's levels (docs/guides/multiplayer.md#levels): the host's level starts,
// checkpoint restarts and mission ends told to the guests, the host's steps held for the loading barrier, the guest's
// game sent to the host's level, and the movies' wait. Coney's own; the original has no network play.

namespace coney::platform {

class GameSession;
class NetPlay;

/// A level and the checkpoint a game started it at.
struct LevelAt {
    std::string level;  ///< as the game names it (`level99`, or a mod's level path)
    int checkpoint = 1; ///< from 1

    friend bool operator==(const LevelAt&, const LevelAt&) = default;
};

/// How many missions a game has ended, each way.
struct MissionCounts {
    std::uint64_t passed = 0; ///< mission-complete screens launched
    std::uint64_t failed = 0; ///< mission-failed screens launched
    std::uint64_t quit =
        0; ///< levels left through the pause menu's four quits (the failed and Rumble result screens' are #1583)
};

/// What NetLevels needs of the running game: the game session in the run (SessionLevelGame), a fake in tests. Read
/// once a step, before the step.
class LevelGame {
  public:
    LevelGame() = default;
    virtual ~LevelGame() = default;
    LevelGame(const LevelGame&) = delete;
    LevelGame& operator=(const LevelGame&) = delete;
    LevelGame(LevelGame&&) = delete;
    LevelGame& operator=(LevelGame&&) = delete;

    /// Whether the next step is a frame of play (a level loaded, past its loading screen and intro movie).
    [[nodiscard]] virtual bool inPlay() const = 0;
    /// How many level loads the game has started (a level change and a checkpoint restart count alike).
    [[nodiscard]] virtual std::uint64_t levelStarts() const = 0;
    /// The level and checkpoint of the newest of those loads; nothing before the first.
    [[nodiscard]] virtual std::optional<LevelAt> currentLevel() const = 0;
    /// How many missions the game has ended, passed and failed.
    [[nodiscard]] virtual MissionCounts missionEnds() const = 0;
    /// Asks the game to load `level` at `checkpoint`, between two frames; false, changing nothing, when it cannot now
    /// (the level table not read yet, or no such level).
    virtual bool jumpToLevel(std::string_view level, int checkpoint) = 0;
    /// Asks the game to leave its level for the front end, between two frames; false, changing nothing, when no level
    /// is in play or loading.
    virtual bool leaveLevel() = 0;
};

/// The game session as NetLevels sees it: whichever session `session` returns (null: none yet), so a session the debug
/// menus start later is followed too.
class SessionLevelGame final : public LevelGame {
  public:
    /// Reads the session `session` returns each time.
    explicit SessionLevelGame(std::function<GameSession*()> session) : m_session(std::move(session)) {}
    [[nodiscard]] bool inPlay() const override;
    [[nodiscard]] std::uint64_t levelStarts() const override;
    [[nodiscard]] std::optional<LevelAt> currentLevel() const override;
    [[nodiscard]] MissionCounts missionEnds() const override;
    bool jumpToLevel(std::string_view level, int checkpoint) override;
    bool leaveLevel() override;

  private:
    std::function<GameSession*()> m_session;
};

/// A network session's levels in the running game, between NetPlay and the game (docs/guides/multiplayer.md#levels).
/// Its beforeStep() runs once a step, after network play has received:
///
/// - **Hosting** (or offline, so a host later tells the guests that join): each level load the game starts is
///   NetPlay::loadLevel() with its network name (net::levelNameOf()), and each mission-complete or mission-failed
///   screen is NetPlay::endMission(). holdsStep() holds a frame of play while a guest still loads the host's level: the
///   host loads and plays its intro movie at its own pace, then waits at the level's first frame of play.
/// - **Joined**: the host's newest level order sends this game there (LevelGame::jumpToLevel(), asked again each step
///   until it is taken), and the order is answered (NetPlay::levelLoaded()) at the first frame of play of the load it
///   started. A join order for the level and checkpoint the game already plays or loads needs no new load. The host's
///   mission ends are noted in the log.
///
/// movieSync() is what the game's movie player tells and asks (movies::MovieSync): the host's movies wait for every
/// guest watching, and a guest's own copy of the host's movie says when it began and when it ended.
class NetLevels {
  public:
    /// Over `net`, which must outlive it; no game until setGame().
    explicit NetLevels(NetPlay& net) : m_net(net) {}

    /// The game whose levels go with the session (null: none, as before a session starts); it must outlive its use.
    void setGame(LevelGame* game) { m_game = game; }
    /// Once a step, after NetPlay::beforeStep() and before the step gate (FrameHooks::beforeStep).
    void beforeStep();
    /// Whether the step about to run is held for the loading barrier: hosting, a frame of play while a joined guest
    /// still loads the host's level (NetPlay::holdsStep()). The main loop's step gate asks it, through
    /// NetGame::holdsStep().
    [[nodiscard]] bool holdsStep() const;
    /// What the game's movie player tells network play and asks of it (MovieMode::setSync()).
    [[nodiscard]] movies::MovieSync movieSync();

  private:
    // Hosting or offline: tells network play of the game's new level loads and mission ends.
    void reportHostGame();
    // Joined: the host quit its level: the game leaves for the front end, and an order of that level (taken or still in
    // the client) or a load of it still going is answered as over.
    void followHostQuit(const net::MissionEnd& end);
    // Joined: takes the client's newest level order, if any, as the one to carry out.
    void adoptPendingOrder();
    // Joined: takes the host's level order, sends the game there and answers it once loaded.
    void followHost();
    // Joined: sends the MovieBegan and MovieDone of this game's own copy of a movie once the host's start of it is
    // known.
    void reportMovie();

    NetPlay& m_net;
    LevelGame* m_game = nullptr;
    std::uint64_t m_starts = 0;              // the game's level starts already reported (hosting) or seen (joined)
    MissionCounts m_missions;                // the game's mission ends already reported
    std::optional<net::LoadLevel> m_order;   // joined: the host's order being carried out
    bool m_jumpAsked = false;                // the game took the order's jump (or needed none)
    std::uint64_t m_startsAtJump = 0;        // the game's level starts when it took the jump
    bool m_refusalNoted = false;             // the order's refused jump is in the log
    std::optional<std::string> m_movieBegan; // joined: this game's movie that began, not yet told
    std::optional<std::pair<std::string, bool>> m_movieEnd; // joined: this game's movie that ended, and skipped
    std::optional<std::uint32_t> m_movieBeganAt;            // joined: the host's movie step last told as begun
    std::optional<std::uint32_t> m_movieDone;               // joined: the host's movie step last answered
};

} // namespace coney::platform
