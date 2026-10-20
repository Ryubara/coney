// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string_view>

#include "core/error.h"
#include "net/transport.h"

// No ENet type appears in this header, so the code that holds a transport never includes ENet or the socket headers.

namespace coney::platform {

/// The Transport over UDP, with ENet (MIT; pinned in cmake/deps.cmake). ENet gives the reliable and sequenced
/// channels, fragmentation, its own resends and its own connection timeouts. Coney's own; the original has no network
/// play (docs/guides/multiplayer.md#enet).
///
/// The deliveries map onto ENet's flags: ReliableOrdered is ENET_PACKET_FLAG_RELIABLE on channel 0,
/// UnreliableSequenced no flag on channel 1, Unreliable ENET_PACKET_FLAG_UNSEQUENCED on channel 1, and an unreliable
/// packet too big for one datagram also gets ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT, so its fragments are not resent.
///
/// poll() never blocks (enet_host_service with no wait). ENet keeps its own time for its resends and timeouts, the
/// one clock Coney does not feed; advance() is therefore ignored. Nothing here opens a dialog: listening on every
/// interface may make an operating system's firewall ask once, so the tests listen on 127.0.0.1 only.
class EnetTransport final : public net::Transport {
  public:
    /// A transport with no socket yet; listen() or connect() opens one. Starts ENet with the first transport.
    EnetTransport();
    EnetTransport(const EnetTransport&) = delete;
    EnetTransport& operator=(const EnetTransport&) = delete;
    /// Closes every connection at once (a last datagram, no wait) and the socket; stops ENet with the last transport.
    ~EnetTransport() override;

    [[nodiscard]] std::expected<void, Error> listen(const net::ListenConfig& config) override;
    [[nodiscard]] std::expected<net::PeerId, Error> connect(std::string_view address, std::uint16_t port) override;
    [[nodiscard]] std::expected<void, Error> send(net::PeerId peer, net::Delivery delivery,
                                                  std::span<const std::byte> data) override;
    /// Ignored: ENet times itself (see the class comment).
    void advance(std::uint64_t /*elapsedNs*/) override {}
    [[nodiscard]] std::optional<net::TransportEvent> poll() override;
    void flush() override;
    void disconnect(net::PeerId peer) override;
    [[nodiscard]] std::uint16_t boundPort() const override { return m_port; }
    [[nodiscard]] net::TransportStats stats() const override { return m_stats; }

    /// Services the socket once, waiting up to `maxWaitMs` for traffic when no event is queued, and keeps the event it
    /// brings for poll(); returns whether an event is queued. For tests and tools only: the game never blocks on the
    /// network.
    bool waitForEvent(std::uint32_t maxWaitMs);

  private:
    // Turns one ENet event into ours (or nothing, for an event of no interest) and queues it.
    void take(const void* enetEvent);
    // Opens the ENet host: bound to `address` for a server, any port for a client.
    [[nodiscard]] std::expected<void, Error> openHost(const void* address, std::size_t peers);

    void* m_host = nullptr;    // ENetHost*; void keeps ENet out of this header
    std::uint16_t m_port = 0;  // the bound port while listening
    bool m_connecting = false; // connect() was called: a client
    net::PeerId m_nextPeer = 1;
    std::map<net::PeerId, void*> m_peers; // our id to its ENetPeer*, for connections not yet gone
    std::deque<net::TransportEvent> m_events;
    net::TransportStats m_stats;
};

} // namespace coney::platform
