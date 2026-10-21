// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "core/error.h"
#include "net/link_quality.h"
#include "net/protocol.h"
#include "net/transport.h"

namespace coney::net {

/// How a NetClient runs. Durations are counted in the game's fixed steps, never in real time.
struct ClientConfig {
    std::string playerName = "Player";       ///< sent in Hello; the server refuses one that is not validPlayerName()
    std::uint32_t pingIntervalSteps = 6;     ///< a Ping to the server this often (5 a second at 30 Hz)
    std::uint32_t timeoutSteps = 300;        ///< the server heard from for this long is taken for gone (10 s at 30 Hz)
    std::uint32_t connectTimeoutSteps = 150; ///< giving up on a connection or handshake after this long (5 s at 30 Hz)
};

/// Where a client's session is.
enum class ClientState : std::uint8_t {
    Idle,        ///< never connected
    Connecting,  ///< waiting for the transport's connection
    Handshaking, ///< connected, Hello sent, waiting for Welcome or Reject
    Connected,   ///< joined: Welcome received
    Disconnected ///< ended; end() says why
};

/// Why a client's session ended.
enum class ClientEnd : std::uint8_t {
    None,           ///< not ended
    Left,           ///< disconnect() was called
    Unreachable,    ///< the transport could not connect (nobody there, or a connection refused)
    TimedOut,       ///< no connection, Welcome or traffic in time
    Rejected,       ///< the server sent Reject; rejectReason() says why
    ServerClosed,   ///< the server sent Bye (it stopped, or dropped this client); byeReason() says why
    ConnectionLost, ///< the connection went with no Bye
    ProtocolError,  ///< the server sent something that does not decode or comes out of turn
};

/// A client's session layer: connects, says Hello, waits for Welcome or Reject, keeps the connection alive with
/// Pings and notices when the server goes quiet, all counted in fixed steps. It knows nothing of the game; later
/// layers send input commands and receive snapshots over the same connection.
/// Coney's own; the original has no network play (docs/guides/multiplayer.md#sessions).
///
/// Call order as NetServer's: receive() before each step, send() after it, advance() once a frame.
class NetClient {
  public:
    /// A client on `transport`, which must outlive it.
    NetClient(Transport& transport, ClientConfig config);
    NetClient(const NetClient&) = delete;
    NetClient& operator=(const NetClient&) = delete;
    /// Leaves (disconnect()) if connected.
    ~NetClient();

    /// Starts connecting to the server at `address`:`port`. Fails as Transport::connect() does, or with
    /// InvalidArgument when a session is under way (Connecting, Handshaking or Connected).
    [[nodiscard]] std::expected<void, Error> connect(std::string_view address, std::uint16_t port);
    /// Says Bye and closes the connection; the session ends as Left. Nothing happens when no session is under way.
    void disconnect();

    /// Passes the frame's elapsed time to the transport and to the round-trip clock.
    void advance(std::uint64_t elapsedNs);
    /// One step's receiving: every waiting transport event and message, then the step's timeouts.
    void receive();
    /// One step's sending: a Ping when due, then a flush.
    void send();

    [[nodiscard]] ClientState state() const { return m_state; }
    [[nodiscard]] ClientEnd end() const { return m_end; }
    /// The server's reason, for ClientEnd::Rejected.
    [[nodiscard]] std::optional<RejectReason> rejectReason() const { return m_rejectReason; }
    /// The server's reason, for ClientEnd::ServerClosed.
    [[nodiscard]] std::optional<ByeReason> byeReason() const { return m_byeReason; }
    /// What Welcome said, once Connected (and after, until the next connect()).
    [[nodiscard]] std::uint8_t playerSlot() const { return m_welcome.playerSlot; }
    [[nodiscard]] std::uint16_t stepRateHz() const { return m_welcome.stepRateHz; }
    [[nodiscard]] const std::string& serverName() const { return m_welcome.serverName; }
    /// The round trip and loss to the server.
    [[nodiscard]] const LinkQuality& quality() const { return m_quality; }
    /// One line for the Network page and the log: the state, and for an ended session why.
    [[nodiscard]] std::string describe() const;
    [[nodiscard]] const ClientConfig& config() const { return m_config; }

  private:
    // Handles one decoded message from the server.
    void handle(const Message& message);
    // Ends the session as `end`, closing the connection (after a Bye when `bye`).
    void finish(ClientEnd end, bool bye, ByeReason reason = ByeReason::Quit);
    // Sends a message to the server; a failure means the connection is gone, which the transport will report.
    void sendToServer(const Message& message);

    Transport& m_transport;
    ClientConfig m_config;
    ClientState m_state = ClientState::Idle;
    ClientEnd m_end = ClientEnd::None;
    std::optional<RejectReason> m_rejectReason;
    std::optional<ByeReason> m_byeReason;
    PeerId m_peer = 0;
    Welcome m_welcome;
    LinkQuality m_quality;
    std::uint64_t m_nowNs = 0;
    std::uint32_t m_stepsInState = 0; // steps since the state last changed, for the connect timeout
    std::uint32_t m_stepsSinceHeard = 0;
    std::uint64_t m_steps = 0; // steps since joining, for the ping schedule
};

} // namespace coney::net
