// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "net/protocol.h"

// The server's loading barrier and movie wait: which joined players have loaded the host's current level, who has gone
// over the load time-out, and whether every player watching the host's movie has ended or skipped it. Plain data with
// no clock, counted in the server's steps; NetServer owns one and sends the messages. Coney's own; the original has no
// network play (docs/guides/multiplayer.md#levels).

namespace coney::net {

/// How long the barrier waits, in the server's fixed steps.
/// @coney-only network play; the original has none
struct LevelBarrierConfig {
    /// A joined player still loading after this many steps is dropped. 60 s at 30 Hz, Coney's choice: four times the
    /// original's preload budget of 15 s for a story level (docs/research/level-loading.md#the-level-record), for a
    /// slow disc or a busy machine.
    std::uint32_t loadTimeoutSteps = 1800;
    /// After the host's own copy of a movie ends, the most steps it waits for the players still watching theirs before
    /// it goes on without them (they are not dropped). 10 s at 30 Hz, Coney's choice.
    std::uint32_t movieTimeoutSteps = 300;
};

/// What LevelBarrier::loaded() made of a player's LevelLoaded.
/// @coney-only network play; the original has none
enum class LoadReply : std::uint8_t {
    Accepted, ///< the current load: the player is in the level
    Late,     ///< an earlier load, overtaken by a newer order: ignored
    Invalid,  ///< a load never ordered, or from a player not joined: the connection is broken or lying
};

/// The server's side of level loads, checkpoints and movies, by player slot. The host's game says which level it
/// loads (load()), the server tells each joined player (the order, or joinOrder() for one who joins later) and passes
/// each LevelLoaded to loaded(); ready() says whether the host may run its next step. Simulation-side state, owned by
/// the session.
/// @coney-only network play; the original has none
class LevelBarrier {
  public:
    /// A barrier with no level and no players.
    explicit LevelBarrier(LevelBarrierConfig config = {});

    /// The host's game starts loading `level` at `checkpoint` (1 or more; a name validLevelName() accepts, else
    /// CONEY_ASSERT): the order for every joined player, numbered one more than the last, a Restart when it is the
    /// level already current and a Change otherwise. Every joined player is loading it from now.
    const LoadLevel& load(const std::string& level, std::uint16_t checkpoint);
    /// The current level's order; nothing before the first load().
    [[nodiscard]] const std::optional<LoadLevel>& current() const { return m_current; }
    /// The current order as a player who joins now gets it (reason Join); the level must be current (CONEY_ASSERT).
    [[nodiscard]] LoadLevel joinOrder() const;

    /// A player has joined in `slot`: loading the current level, when there is one.
    void playerJoined(std::uint8_t slot);
    /// The player in `slot` has gone: no longer waited for, in a load or a movie.
    void playerLeft(std::uint8_t slot);
    /// The player in `slot` says they have loaded `load`.
    [[nodiscard]] LoadReply loaded(std::uint8_t slot, std::uint32_t load);
    /// Whether the player in `slot` is joined and still loading the current level.
    [[nodiscard]] bool loading(std::uint8_t slot) const;
    /// Whether the host may step: every joined player has loaded the current level (always, with no level).
    [[nodiscard]] bool ready() const;

    /// One server step: each loading player's wait and the movie's time-out count on. Returns the slots now loading
    /// for more than LevelBarrierConfig::loadTimeoutSteps, in rising order, for the server to drop.
    [[nodiscard]] std::vector<std::uint8_t> step();

    /// The host's game has started a movie, whose MovieStart event came in the server's `movieStep`: every joined
    /// player who has loaded the level is watching it. Replaces any movie before.
    void startMovie(std::uint32_t movieStep);
    /// The host's own copy of the movie has ended (or the host skipped it): the time-out starts counting.
    void endHostMovie();
    /// The player in `slot` says their copy of the movie of `movieStep` ended, or was skipped. A movie that is not the
    /// current one's (late, or before it) changes nothing.
    void movieDone(std::uint8_t slot, std::uint32_t movieStep, bool skipped);
    /// The player in `slot` says their own copy of the movie of `movieStep` has begun (stub: nothing yet).
    void movieBegan(std::uint8_t slot, std::uint32_t movieStep);
    /// Whether a player skipped the current movie: the host then ends its own copy, as any pad's skip does.
    [[nodiscard]] bool movieSkipped() const { return m_movie.has_value() && m_movie->skipped; }
    /// Whether the host may go on from its movie: no movie, or its own copy has ended and every player watching has
    /// ended theirs, or LevelBarrierConfig::movieTimeoutSteps have passed since its own ended.
    [[nodiscard]] bool movieMayEnd() const;

  private:
    // The movie being waited for: its step, who is still watching, whether one skipped and the host's own end.
    struct Movie {
        std::uint32_t step = 0;
        std::set<std::uint8_t> watching;
        bool skipped = false;
        bool hostEnded = false;
        std::uint32_t stepsSinceHostEnded = 0;
    };

    LevelBarrierConfig m_config;
    std::optional<LoadLevel> m_current;
    std::uint32_t m_loads = 0;                      // the last load's number
    std::map<std::uint8_t, std::uint32_t> m_joined; // each joined slot's steps loading; 0 when loaded
    std::set<std::uint8_t> m_loading;               // the joined slots still loading the current level
    std::optional<Movie> m_movie;
};

} // namespace coney::net
