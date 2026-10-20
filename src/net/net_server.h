// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "core/error.h"
#include "net/link_quality.h"
#include "net/protocol.h"
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
};

/// Something the game should know happened, from takeEvents().
struct ServerEvent {
    enum class Kind : std::uint8_t {
        Joined, ///< a player finished the handshake
        Left,   ///< a joined player is gone (`reason` says how; ConnectionLost when the transport dropped it)
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
    [[nodiscard]] bool running() const { return m_running; }

    /// Passes the frame's elapsed time to the transport and to the round-trip clock.
    void advance(std::uint64_t elapsedNs);
    /// One step's receiving: every waiting transport event and message, then the step's timeouts.
    void receive();
    /// One step's sending: the Pings that are due, then a flush.
    void send();

    /// Every connection, joined or not, in the order they connected.
    [[nodiscard]] const std::vector<ServerClient>& clients() const { return m_clients; }
    /// Joined players.
    [[nodiscard]] std::size_t joinedCount() const;
    /// The events since the last call, oldest first.
    [[nodiscard]] std::vector<ServerEvent> takeEvents();
    /// The port the server listens on (the free one for port 0); 0 when not running.
    [[nodiscard]] std::uint16_t port() const { return m_running ? m_transport.boundPort() : 0; }
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
};

} // namespace coney::net
