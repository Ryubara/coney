// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/enet_transport.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// ENet's Windows header includes <winsock2.h>, and with it <windows.h>, and switches some warnings off for the rest of
// the file; keep both from leaking into this file's own code.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
#if defined(_MSC_VER)
#pragma warning(push)
#endif
#include <enet/enet.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "core/assert.h"
#include "core/error.h"
#include "net/transport.h"

namespace coney::platform {

namespace {

// ENet channels: one reliable and ordered, one for both unreliable kinds (sequencing is per channel in ENet).
constexpr enet_uint8 kReliableChannel = 0;
constexpr enet_uint8 kUnreliableChannel = 1;
constexpr std::size_t kChannels = 2;

// Transports alive, so ENet starts with the first and stops with the last (single-threaded, like the main loop).
int g_enetUsers = 0;

// The ENet host behind the opaque pointer the header keeps.
ENetHost* host(void* opaque) { return static_cast<ENetHost*>(opaque); }

// Fills an ENet address from a host name or a numeric address and a port; false when it does not resolve.
bool resolve(std::string_view name, std::uint16_t port, ENetAddress& address) {
    address.host = ENET_HOST_ANY;
    address.port = port;
    if (name.empty()) {
        return true;
    }
    const std::string text(name);
    return enet_address_set_host(&address, text.c_str()) == 0;
}

} // namespace

EnetTransport::EnetTransport() {
    if (g_enetUsers++ == 0) {
        // enet_initialize only fails when the socket library will not start (WSAStartup on Windows); every later call
        // then fails with PlatformFailure, since no host can be made.
        (void)enet_initialize();
    }
}

EnetTransport::~EnetTransport() {
    if (m_host != nullptr) {
        // A last datagram to each peer so the far end learns at once rather than by timing out.
        for (const auto& [id, peer] : m_peers) {
            enet_peer_disconnect_now(static_cast<ENetPeer*>(peer), 0);
        }
        enet_host_destroy(host(m_host));
    }
    if (--g_enetUsers == 0) {
        enet_deinitialize();
    }
}

std::expected<void, Error> EnetTransport::openHost(const void* address, std::size_t peers) {
    ENetHost* made = enet_host_create(static_cast<const ENetAddress*>(address), peers, kChannels, 0, 0);
    if (made == nullptr) {
        return fail(ErrorCode::PlatformFailure, "could not open a UDP socket (is the port in use?)");
    }
    m_host = made;
    return {};
}

std::expected<void, Error> EnetTransport::listen(const net::ListenConfig& config) {
    if (m_host != nullptr || m_connecting) {
        return fail(ErrorCode::InvalidArgument, "the transport is already listening or connecting");
    }
    ENetAddress address{};
    if (!resolve(config.bindAddress, config.port, address)) {
        return fail(ErrorCode::InvalidArgument, "cannot resolve the address to listen on: " + config.bindAddress);
    }
    if (auto opened = openHost(&address, config.maxPeers); !opened) {
        return opened;
    }
    // Port 0 asks for any free port; the socket knows which it got.
    ENetAddress bound{};
    if (enet_socket_get_address(host(m_host)->socket, &bound) != 0) {
        return fail(ErrorCode::PlatformFailure, "cannot read the socket's port");
    }
    m_port = bound.port;
    return {};
}

std::expected<net::PeerId, Error> EnetTransport::connect(std::string_view address, std::uint16_t port) {
    if (m_port != 0) {
        return fail(ErrorCode::InvalidArgument, "a listening transport cannot connect");
    }
    ENetAddress target{};
    if (address.empty() || !resolve(address, port, target)) {
        return fail(ErrorCode::InvalidArgument, "cannot resolve the server's address: " + std::string(address));
    }
    // A client's host: any local port, one connection.
    if (m_host == nullptr) {
        if (auto opened = openHost(nullptr, 1); !opened) {
            return std::unexpected(opened.error());
        }
    }
    m_connecting = true;
    ENetPeer* peer = enet_host_connect(host(m_host), &target, kChannels, 0);
    if (peer == nullptr) {
        return fail(ErrorCode::PlatformFailure, "no free connection to connect with");
    }
    const net::PeerId id = m_nextPeer++;
    m_peers[id] = peer;
    return id;
}

std::expected<void, Error> EnetTransport::send(net::PeerId peer, net::Delivery delivery,
                                               std::span<const std::byte> data) {
    CONEY_ASSERT(!data.empty());
    const auto found = m_peers.find(peer);
    if (found == m_peers.end()) {
        return fail(ErrorCode::NotFound, "connection is not up");
    }
    auto* enetPeer = static_cast<ENetPeer*>(found->second);
    enet_uint32 flags = 0;
    enet_uint8 channel = kUnreliableChannel;
    switch (delivery) {
    case net::Delivery::ReliableOrdered:
        flags = ENET_PACKET_FLAG_RELIABLE;
        channel = kReliableChannel;
        break;
    case net::Delivery::UnreliableSequenced:
        break;
    case net::Delivery::Unreliable:
        flags = ENET_PACKET_FLAG_UNSEQUENCED;
        break;
    }
    // ENet would send an unreliable packet's fragments reliably unless told otherwise; a lost snapshot fragment
    // should cost the snapshot, not stall the channel.
    const std::size_t fragmentBytes = enetPeer->mtu - sizeof(ENetProtocolHeader) - sizeof(ENetProtocolSendFragment);
    if (delivery != net::Delivery::ReliableOrdered && data.size() > fragmentBytes) {
        flags |= ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
    }
    ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
    if (packet == nullptr) {
        return fail(ErrorCode::PlatformFailure, "out of memory for a packet");
    }
    // enet_peer_send refuses a peer that is not connected (still connecting, or closing); the packet is then ours.
    if (enet_peer_send(enetPeer, channel, packet) != 0) {
        enet_packet_destroy(packet);
        return fail(ErrorCode::NotFound, "connection is not up");
    }
    m_stats.packetsSent += 1;
    m_stats.bytesSent += data.size();
    return {};
}

std::optional<net::TransportEvent> EnetTransport::poll() {
    // Service the socket without waiting until an event of interest comes or nothing is left.
    ENetEvent event{};
    while (m_events.empty() && m_host != nullptr && enet_host_service(host(m_host), &event, 0) > 0) {
        take(&event);
    }
    if (m_events.empty()) {
        return std::nullopt;
    }
    net::TransportEvent next = std::move(m_events.front());
    m_events.pop_front();
    return next;
}

bool EnetTransport::waitForEvent(std::uint32_t maxWaitMs) {
    if (m_host == nullptr) {
        return !m_events.empty();
    }
    // Always services the socket once (acknowledgements go out), waiting only when nothing is queued yet.
    ENetEvent event{};
    if (enet_host_service(host(m_host), &event, m_events.empty() ? maxWaitMs : 0) > 0) {
        take(&event);
    }
    return !m_events.empty();
}

void EnetTransport::take(const void* enetEvent) {
    const auto& event = *static_cast<const ENetEvent*>(enetEvent);
    // Our id for the event's peer; 0 when we have none (a new incoming connection).
    net::PeerId id = 0;
    for (const auto& [known, peer] : m_peers) {
        if (peer == event.peer) {
            id = known;
            break;
        }
    }
    switch (event.type) {
    case ENET_EVENT_TYPE_CONNECT:
        if (id == 0) {
            id = m_nextPeer++;
            m_peers[id] = event.peer;
        }
        m_events.push_back(net::TransportEvent{.kind = net::TransportEventKind::Connected, .peer = id, .data = {}});
        break;
    case ENET_EVENT_TYPE_DISCONNECT:
        if (id != 0) {
            m_peers.erase(id);
            m_events.push_back(
                net::TransportEvent{.kind = net::TransportEventKind::Disconnected, .peer = id, .data = {}});
        }
        break;
    case ENET_EVENT_TYPE_RECEIVE: {
        if (id != 0) {
            const auto bytes = std::as_bytes(std::span<const enet_uint8>(event.packet->data, event.packet->dataLength));
            m_stats.packetsReceived += 1;
            m_stats.bytesReceived += bytes.size();
            m_events.push_back(net::TransportEvent{.kind = net::TransportEventKind::Received,
                                                   .peer = id,
                                                   .data = std::vector<std::byte>(bytes.begin(), bytes.end())});
        }
        enet_packet_destroy(event.packet);
        break;
    }
    case ENET_EVENT_TYPE_NONE:
        break;
    }
}

void EnetTransport::flush() {
    if (m_host != nullptr) {
        enet_host_flush(host(m_host));
    }
}

void EnetTransport::disconnect(net::PeerId peer) {
    const auto found = m_peers.find(peer);
    if (found != m_peers.end()) {
        // After the packets already queued; ENet reports the disconnect once the far end acknowledges or times out.
        enet_peer_disconnect_later(static_cast<ENetPeer*>(found->second), 0);
    }
}

} // namespace coney::platform
