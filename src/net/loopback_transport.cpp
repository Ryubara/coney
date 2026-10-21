// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/loopback_transport.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "core/error.h"
#include "net/transport.h"

namespace coney::net {

LoopbackNetwork::~LoopbackNetwork() { CONEY_ASSERT(m_transports == 0); }

LoopbackTransport::LoopbackTransport(LoopbackNetwork& network, std::string address)
    : m_network(network), m_address(std::move(address)) {
    ++m_network.m_transports;
}

// NOLINTNEXTLINE(bugprone-exception-escape): closing can only throw on allocation, which ends the program anyway
LoopbackTransport::~LoopbackTransport() {
    // The far ends are live transports on the same network (they unhook themselves from us when they go first).
    while (!m_peers.empty()) {
        disconnect(m_peers.begin()->first);
    }
    if (m_port != 0) {
        m_network.m_listeners.erase(key(m_address, m_port));
    }
    --m_network.m_transports;
}

std::string LoopbackTransport::key(std::string_view address, std::uint16_t port) {
    return std::string(address) + ":" + std::to_string(port);
}

std::expected<void, Error> LoopbackTransport::listen(const ListenConfig& config) {
    if (m_port != 0 || m_connecting) {
        return fail(ErrorCode::InvalidArgument, "loopback transport is already listening or connecting");
    }
    std::uint16_t port = config.port;
    if (port == 0) {
        // Any free port: the next of the dynamic range nobody on this network holds.
        while (m_network.m_listeners.contains(key(m_address, m_network.m_nextFreePort))) {
            ++m_network.m_nextFreePort;
        }
        port = m_network.m_nextFreePort++;
    }
    if (m_network.m_listeners.contains(key(m_address, port))) {
        return fail(ErrorCode::PlatformFailure, "loopback port " + std::to_string(port) + " is in use");
    }
    m_network.m_listeners[key(m_address, port)] = this;
    m_port = port;
    m_maxPeers = config.maxPeers;
    return {};
}

std::expected<PeerId, Error> LoopbackTransport::connect(std::string_view address, std::uint16_t port) {
    if (m_port != 0) {
        return fail(ErrorCode::InvalidArgument, "a listening loopback transport cannot connect");
    }
    m_connecting = true;
    const auto found = m_network.m_listeners.find(key(address, port));
    if (found == m_network.m_listeners.end() || found->second->m_peers.size() >= found->second->m_maxPeers) {
        // Nobody there or no room: the connection fails as an unanswered one does, with an id that is gone at once.
        const PeerId id = m_nextPeer++;
        m_inbox.push_back(TransportEvent{.kind = TransportEventKind::Disconnected, .peer = id, .data = {}});
        return id;
    }
    // Both ends exist at once, each reporting Connected to its own poll().
    LoopbackTransport& server = *found->second;
    const PeerId id = m_nextPeer++;
    const PeerId idThere = server.attach(*this, id);
    m_peers[id] = Remote{.transport = &server, .idThere = idThere};
    m_inbox.push_back(TransportEvent{.kind = TransportEventKind::Connected, .peer = id, .data = {}});
    return id;
}

PeerId LoopbackTransport::attach(LoopbackTransport& remote, PeerId idThere) {
    const PeerId id = m_nextPeer++;
    m_peers[id] = Remote{.transport = &remote, .idThere = idThere};
    m_inbox.push_back(TransportEvent{.kind = TransportEventKind::Connected, .peer = id, .data = {}});
    return id;
}

std::expected<void, Error> LoopbackTransport::send(PeerId peer, Delivery /*delivery*/,
                                                   std::span<const std::byte> data) {
    CONEY_ASSERT(!data.empty());
    const auto found = m_peers.find(peer);
    if (found == m_peers.end()) {
        return fail(ErrorCode::NotFound, "loopback connection is not up");
    }
    // Every delivery kind arrives at once and in order, which satisfies all three contracts.
    LoopbackTransport& remote = *found->second.transport;
    remote.m_inbox.push_back(TransportEvent{.kind = TransportEventKind::Received,
                                            .peer = found->second.idThere,
                                            .data = std::vector<std::byte>(data.begin(), data.end())});
    m_stats.packetsSent += 1;
    m_stats.bytesSent += data.size();
    remote.m_stats.packetsReceived += 1;
    remote.m_stats.bytesReceived += data.size();
    return {};
}

std::optional<TransportEvent> LoopbackTransport::poll() {
    if (m_inbox.empty()) {
        return std::nullopt;
    }
    TransportEvent event = std::move(m_inbox.front());
    m_inbox.pop_front();
    return event;
}

void LoopbackTransport::disconnect(PeerId peer) {
    const auto found = m_peers.find(peer);
    if (found == m_peers.end()) {
        return;
    }
    const Remote remote = found->second;
    m_peers.erase(found);
    // Packets already sent are in the far end's queue ahead of this, as reliable packets queued before a close are.
    remote.transport->detached(remote.idThere);
    m_inbox.push_back(TransportEvent{.kind = TransportEventKind::Disconnected, .peer = peer, .data = {}});
}

void LoopbackTransport::detached(PeerId peer) {
    if (m_peers.erase(peer) != 0) {
        m_inbox.push_back(TransportEvent{.kind = TransportEventKind::Disconnected, .peer = peer, .data = {}});
    }
}

} // namespace coney::net
