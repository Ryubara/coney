// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/error.h"
#include "net/transport.h"

namespace coney::net {

class LoopbackTransport;

/// The in-memory "network" loopback transports connect through: who listens at which address and port. Several
/// transports on one network stand for several machines, so a test runs a server and its clients in one process
/// with no socket. It must outlive every transport made on it (CONEY_ASSERT in its destructor).
class LoopbackNetwork {
  public:
    LoopbackNetwork() = default;
    LoopbackNetwork(const LoopbackNetwork&) = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;
    ~LoopbackNetwork();

  private:
    friend class LoopbackTransport;
    std::map<std::string, LoopbackTransport*> m_listeners; // "address:port" to the transport listening there
    std::uint16_t m_nextFreePort = 49152;                  // what listening on port 0 gets: the dynamic range
    int m_transports = 0;                                  // live transports, for the destructor's check
};

/// A Transport whose packets go straight into the other end's queue: no loss, no delay, no reordering, every
/// delivery kind in send order. Put a LinkConditioner in front of it to test bad networks. Coney's own test aid.
class LoopbackTransport final : public Transport {
  public:
    /// A transport on `network` at `address`, the name other transports connect() to when this one listens.
    LoopbackTransport(LoopbackNetwork& network, std::string address);
    LoopbackTransport(const LoopbackTransport&) = delete;
    LoopbackTransport& operator=(const LoopbackTransport&) = delete;
    /// Closes every connection (the other ends see Disconnected) and stops listening.
    ~LoopbackTransport() override;

    [[nodiscard]] std::expected<void, Error> listen(const ListenConfig& config) override;
    /// Connects to the transport listening at `address`:`port`. With none there, or one that is full, poll() reports
    /// the connection Disconnected at once, as a refused or unanswered connection on a real network.
    [[nodiscard]] std::expected<PeerId, Error> connect(std::string_view address, std::uint16_t port) override;
    [[nodiscard]] std::expected<void, Error> send(PeerId peer, Delivery delivery,
                                                  std::span<const std::byte> data) override;
    /// Nothing to do: the loopback has no delays.
    void advance(std::uint64_t /*elapsedNs*/) override {}
    [[nodiscard]] std::optional<TransportEvent> poll() override;
    /// Nothing to do: send() delivers at once.
    void flush() override {}
    void disconnect(PeerId peer) override;
    [[nodiscard]] std::uint16_t boundPort() const override { return m_port; }
    [[nodiscard]] TransportStats stats() const override { return m_stats; }

  private:
    // The far end of one of this transport's connections.
    struct Remote {
        LoopbackTransport* transport = nullptr;
        PeerId idThere = 0; // what the far end calls this connection
    };

    // Makes the near end of a connection to `remote`, reporting Connected; returns its id.
    PeerId attach(LoopbackTransport& remote, PeerId idThere);
    // Called by the far end when it closes connection `peer` of ours: forget it and report Disconnected.
    void detached(PeerId peer);
    // The "address:port" key of the network's listener map.
    [[nodiscard]] static std::string key(std::string_view address, std::uint16_t port);

    LoopbackNetwork& m_network;
    std::string m_address;
    std::uint16_t m_port = 0;   // 0 while not listening
    std::size_t m_maxPeers = 0; // while listening
    bool m_connecting = false;  // connect() was called: this transport is a client
    PeerId m_nextPeer = 1;      // ids are never reused
    std::map<PeerId, Remote> m_peers;
    std::deque<TransportEvent> m_inbox;
    TransportStats m_stats;
};

} // namespace coney::net
