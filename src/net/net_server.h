// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/error.h"
#include "core/pad.h"
#include "net/event_channel.h"
#include "net/input_buffer.h"
#include "net/level_barrier.h"
#include "net/link_quality.h"
#include "net/protocol.h"
#include "net/snapshot.h"
#include "net/transport.h"

namespace coney::net {

/// How a NetServer runs. Durations are counted in the game's fixed steps, never in real time.
struct ServerConfig {
    std::uint16_t port = kDefaultPort;         ///< 0: any free port (the tests)
    std::string bindAddress;                   ///< empty: every interface; see ListenConfig
    std::size_t maxClients = 3;                ///< remote players at once, besides the host (Coney's choice)
    std::string serverName = "Coney";          ///< sent in Welcome; validPlayerName()
    std::uint16_t stepRateHz = 30;             ///< the simulation's fixed steps a second, sent in Welcome
    std::uint32_t pingIntervalSteps = 6;       ///< a Ping to each player this often (5 a second at 30 Hz)
    std::uint32_t timeoutSteps = 300;          ///< a player heard from for this long is dropped (10 s at 30 Hz)
    std::uint32_t handshakeTimeoutSteps = 150; ///< a connection that sends no valid Hello for this long is closed
    /// How each player's input commands are played on the server's steps: the delay, its slack and when a quiet pad
    /// is let go.
    InputBufferConfig input;
    /// The most bytes each player's Snapshot packet takes (SnapshotSender).
    std::size_t snapshotBudgetBytes = kSnapshotBudgetBytes;
    /// A player still loading the host's level after this many steps is dropped (LevelBarrierConfig).
    std::uint32_t loadTimeoutSteps = LevelBarrierConfig{}.loadTimeoutSteps;
    /// How long the host waits for the players still watching a movie after its own copy ended (LevelBarrierConfig).
    std::uint32_t movieTimeoutSteps = LevelBarrierConfig{}.movieTimeoutSteps;
};

/// One remote player as the server sees it.
struct ServerClient {
    PeerId peer = 0;
    std::uint8_t slot = 0; ///< 0 until joined; then 1 to maxClients, unique among joined players
    std::string name;      ///< empty until joined
    bool joined = false;   ///< sent Welcome; false while the handshake runs
    std::uint32_t stepsSinceHeard = 0;
    std::uint32_t stepsConnected = 0;
    LinkQuality quality;
    InputBuffer input;        ///< their input commands; current() is this step's pad
    SnapshotSender snapshots; ///< the snapshots sent to them, and the newest they have acknowledged
    EventSender events;       ///< the game events sent to them: each delivery's batches numbered from 1
};

/// Something the game should know happened, from takeEvents().
struct ServerEvent {
    enum class Kind : std::uint8_t {
        Joined, ///< a player finished the handshake
        Left,   ///< a joined player is gone (`reason` says how; ConnectionLost when the transport dropped it)
        Loaded, ///< a joined player has loaded the host's current level (LevelBarrier::loaded())
    };
    Kind kind = Kind::Joined;
    std::uint8_t slot = 0;
    std::string name;
    /// For Left: what the player's Bye said, or what the server decided.
    ByeReason reason = ByeReason::Quit;
    bool connectionLost = false; ///< For Left: the transport reported the connection gone with no Bye first.
};

/// The listen server's session layer: accepts connections, runs the handshake (Hello, then Welcome or Reject),
/// keeps each player alive with Pings, and drops players who go quiet, all counted in fixed steps. It knows nothing
/// of the game; later layers send input commands and snapshots over the same connections.
/// Coney's own; the original has no network play (docs/guides/multiplayer.md#sessions).
///
/// Call order each simulation step: receive() before the step, send() after it (so a step's result goes out in the
/// same frame); advance() once a frame with the frame's elapsed time.
class NetServer {
  public:
    /// A server on `transport`, which must outlive it; nothing happens until start().
    NetServer(Transport& transport, ServerConfig config);
    NetServer(const NetServer&) = delete;
    NetServer& operator=(const NetServer&) = delete;
    /// Stops the server if it runs (stop()).
    ~NetServer();

    /// Listens for players. Fails as Transport::listen() does, or with InvalidArgument when already running or the
    /// server name is not valid.
    [[nodiscard]] std::expected<void, Error> start();
    /// Says Bye to every connection and closes them. The transport keeps its socket until it is destroyed, so hosting
    /// again takes a new transport and server.
    void stop();
    /// Whether it listens: between a start() that succeeded and stop().
    [[nodiscard]] bool running() const { return m_running; }

    /// Passes the frame's elapsed time to the transport and to the round-trip clock.
    void advance(std::uint64_t elapsedNs);
    /// One step's receiving: every waiting transport event and message, the step's timeouts, then each joined
    /// player's pad for the step (InputBuffer::take()).
    void receive();
    /// Sends each joined player a Snapshot of `humans` (the humans at the server's `step`) and `huds` (each player
    /// slot's HUD) as a delta against the newest they have acknowledged (SnapshotSender::build()). Called once a step,
    /// after the simulation step and before send(), which flushes it.
    void sendSnapshots(std::uint32_t step, std::span<const SnapshotHuman> humans,
                       std::span<const SnapshotHud> huds = {});
    /// Sends each joined player the game events of the server's `step` (ordinals rising, toNetEvents()) in batches
    /// (EventSender::build()): the reliable ones ReliableOrdered, the one-shots Unreliable. Called once a step, after
    /// the simulation step and before send(), which flushes them. A player who joins later gets none of these.
    void sendEvents(std::uint32_t step, std::span<const NetEvent> events);
    /// One step's sending: the Pings that are due, each joined player's InputTiming with theirs, then a flush.
    void send();

    /// The host's game starts loading `level` at `checkpoint` (LevelBarrier::load()): every joined player is told to
    /// load it (LoadLevel) and gets no snapshots or events until they have; one who joins later is told on joining.
    /// Returns the load's number. Works while not running too, so a host that starts later still tells its players.
    std::uint32_t loadLevel(const std::string& level, std::uint16_t checkpoint);
    /// The host's mission in the current level has ended: every joined player is told how (MissionEnd). Nothing
    /// happens with no level.
    void endMission(MissionOutcome outcome);
    /// The current level's order; nothing before the first loadLevel().
    [[nodiscard]] const std::optional<LoadLevel>& level() const { return m_level.current(); }
    /// Whether the host's game may run its next step: every joined player has loaded the current level (the loading
    /// barrier). A player who has not after ServerConfig::loadTimeoutSteps is dropped, so it never waits for ever.
    [[nodiscard]] bool levelReady() const { return m_level.ready(); }
    /// Whether the player in `slot` is joined and still loading the current level.
    [[nodiscard]] bool loading(std::uint8_t slot) const { return m_level.loading(slot); }
    /// The host's game has started a movie whose MovieStart event came in the server's `movieStep`
    /// (LevelBarrier::startMovie()).
    void startMovie(std::uint32_t movieStep) { m_level.startMovie(movieStep); }
    /// The host's own copy of the movie has ended, or the host skipped it (LevelBarrier::endHostMovie()).
    void endHostMovie() { m_level.endHostMovie(); }
    /// Whether a player skipped the current movie, so the host should end its own copy.
    [[nodiscard]] bool movieSkipped() const { return m_level.movieSkipped(); }
    /// Whether the host may go on from its movie (LevelBarrier::movieMayEnd()).
    [[nodiscard]] bool movieMayEnd() const { return m_level.movieMayEnd(); }

    /// Every connection, joined or not, in the order they connected.
    [[nodiscard]] const std::vector<ServerClient>& clients() const { return m_clients; }
    /// Joined players.
    [[nodiscard]] std::size_t joinedCount() const;
    /// The pad of the player in `slot`, as the host's game should read it this step: nothing when no player has joined
    /// in it; else the pad their input buffer played on this step (InputBuffer::take()).
    [[nodiscard]] std::optional<PadSample> remotePad(std::uint8_t slot) const;
    /// The events since the last call, oldest first.
    [[nodiscard]] std::vector<ServerEvent> takeEvents();
    /// The port the server listens on (the free one for port 0); 0 when not running.
    [[nodiscard]] std::uint16_t port() const { return m_running ? m_transport.boundPort() : 0; }
    /// How it runs, as made.
    [[nodiscard]] const ServerConfig& config() const { return m_config; }

  private:
    // Handles one decoded message from a connection; returns false when the connection was closed for it.
    bool handle(ServerClient& client, const Message& message);
    // Answers a Hello: Welcome and joined, or Reject and closed.
    bool handleHello(ServerClient& client, const Hello& hello);
    // Sends a message to a connection; a failure means it is gone, which the transport will report.
    void sendTo(const ServerClient& client, const Message& message);
    // Says Bye (when `bye`) and closes the connection at `index`, reporting Left if it had joined.
    void close(std::size_t index, ByeReason reason, bool bye);
    // The lowest slot no joined player holds; 0 when every slot is taken.
    [[nodiscard]] std::uint8_t freeSlot() const;
    // The index of the connection `peer`; clients().size() when there is none.
    [[nodiscard]] std::size_t find(PeerId peer) const;

    Transport& m_transport;
    ServerConfig m_config;
    bool m_running = false;
    std::uint64_t m_nowNs = 0;
    std::uint64_t m_steps = 0; // steps since start(), for the ping schedule
    std::vector<ServerClient> m_clients;
    std::vector<ServerEvent> m_events;
    LevelBarrier m_level; // the host's level, who has loaded it, and the movie wait
};

} // namespace coney::net
