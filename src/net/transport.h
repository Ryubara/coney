// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

// The packet layer under Coney's network sessions: what a socket library must offer, and nothing of any socket
// library itself, so the sessions run the same over ENet (src/platform/enet_transport.h) and over the in-memory
// loopback the tests use. Coney's own; the original has no network play (docs/guides/multiplayer.md#transport).

namespace coney::net {

/// One connection as a transport names it: a small number unique among the transport's live and past connections,
/// never reused while the transport lives. 0 is never a connection.
using PeerId = std::uint32_t;

/// How a packet must arrive. A message's kind decides it (docs/guides/multiplayer.md#delivery).
enum class Delivery : std::uint8_t {
    ReliableOrdered,     ///< Exactly once, in send order among reliable packets: the handshake, events, chat.
    UnreliableSequenced, ///< At most once; a packet older than one already delivered is dropped: snapshots.
    Unreliable,          ///< At most once, in any order, possibly duplicated by a link conditioner: pings.
};

/// What poll() reports.
enum class TransportEventKind : std::uint8_t {
    Connected,    ///< A connection is up: one we asked for with connect(), or a new one on a listening transport.
    Disconnected, ///< A connection is gone: closed by either side, timed out, or refused. Its id is never reused.
    Received,     ///< A packet arrived on a connection.
};

/// One event from poll().
struct TransportEvent {
    TransportEventKind kind = TransportEventKind::Received;
    PeerId peer = 0;
    std::vector<std::byte> data; ///< The packet, for Received; empty otherwise.
};

/// Totals since the transport was made, for the Network page. Each counts what this end sent or received.
struct TransportStats {
    std::uint64_t packetsSent = 0;
    std::uint64_t packetsReceived = 0;
    std::uint64_t bytesSent = 0;     ///< Payload bytes, without the transport's own headers.
    std::uint64_t bytesReceived = 0; ///< Payload bytes, without the transport's own headers.
};

/// Where and how to accept connections.
struct ListenConfig {
    std::uint16_t port = 0;   ///< 0: any free port, which boundPort() then reports.
    std::size_t maxPeers = 1; ///< Connections accepted at once; further attempts are refused by the transport.
    /// The local address to listen on: empty for every interface, "127.0.0.1" for this machine only (the tests, so no
    /// firewall prompt opens on a desktop).
    std::string bindAddress;
};

/// A packet transport. Single-threaded and non-blocking: the main loop calls advance(), poll() until it returns
/// nothing, sends, then flush(), every frame. It never reads a clock: whatever needs time (a link conditioner's delays)
/// is told how much passed through advance(), which the main loop feeds with the frame time the frame pacer measured.
class Transport {
  public:
    virtual ~Transport() = default;

    /// Starts accepting connections. Fails with PlatformFailure when the address or port cannot be bound, and with
    /// InvalidArgument when this transport is already listening or connecting.
    [[nodiscard]] virtual std::expected<void, Error> listen(const ListenConfig& config) = 0;

    /// Starts a connection to `address` (a host name or a numeric address) at `port`. The connection is usable once
    /// poll() reports Connected for the returned id, or gone when it reports Disconnected. Fails with
    /// InvalidArgument when the address cannot be resolved or this transport is listening.
    [[nodiscard]] virtual std::expected<PeerId, Error> connect(std::string_view address, std::uint16_t port) = 0;

    /// Queues a packet for `peer`. Fails with NotFound when the connection is not up (never was, or is gone); a
    /// caller treats that as the peer being gone, which poll() will report. An empty packet is a programmer error.
    [[nodiscard]] virtual std::expected<void, Error> send(PeerId peer, Delivery delivery,
                                                          std::span<const std::byte> data) = 0;

    /// Tells the transport that `elapsedNs` nanoseconds passed since the last call. A transport over a real network
    /// keeps its own timers and ignores it; the loopback and the link conditioner run their delays on it.
    virtual void advance(std::uint64_t elapsedNs) = 0;

    /// The next event, or nothing when none is waiting. Never blocks.
    [[nodiscard]] virtual std::optional<TransportEvent> poll() = 0;

    /// Sends everything queued now rather than at the next poll().
    virtual void flush() = 0;

    /// Closes a connection after the reliable packets already queued for it. The other side sees Disconnected; this
    /// side reports Disconnected too, once the close is done. Unknown or closed peers are ignored.
    virtual void disconnect(PeerId peer) = 0;

    /// The port this transport listens on (the free one it was given for port 0); 0 when it is not listening.
    [[nodiscard]] virtual std::uint16_t boundPort() const = 0;

    /// Totals of what went through this transport.
    [[nodiscard]] virtual TransportStats stats() const = 0;
};

} // namespace coney::net
