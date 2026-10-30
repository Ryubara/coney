// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/net_server.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/error.h"
#include "core/pad.h"
#include "net/protocol.h"
#include "net/snapshot.h"
#include "net/transport.h"

namespace coney::net {

NetServer::NetServer(Transport& transport, ServerConfig config) : m_transport(transport), m_config(std::move(config)) {}

// NOLINTNEXTLINE(bugprone-exception-escape): stop() can only throw on allocation, which ends the program anyway
NetServer::~NetServer() { stop(); }

std::expected<void, Error> NetServer::start() {
    if (m_running) {
        return fail(ErrorCode::InvalidArgument, "the server is already running");
    }
    if (!validPlayerName(m_config.serverName)) {
        return fail(ErrorCode::InvalidArgument, "the server name must be 1 to 32 printable ASCII characters");
    }
    // Two connections more than the slots, so a player over the limit still hears "server full" rather than
    // nothing, and a stray connection cannot lock a joiner out while its handshake times out.
    const ListenConfig listen{
        .port = m_config.port, .maxPeers = m_config.maxClients + 2, .bindAddress = m_config.bindAddress};
    if (auto listening = m_transport.listen(listen); !listening) {
        return listening;
    }
    m_running = true;
    m_steps = 0;
    return {};
}

void NetServer::stop() {
    if (!m_running) {
        return;
    }
    while (!m_clients.empty()) {
        close(m_clients.size() - 1, ByeReason::Quit, true);
    }
    m_transport.flush();
    m_running = false;
}

void NetServer::advance(std::uint64_t elapsedNs) {
    m_nowNs += elapsedNs;
    m_transport.advance(elapsedNs);
}

void NetServer::receive() {
    if (!m_running) {
        return;
    }
    // Every event waiting now; the transport never blocks.
    while (std::optional<TransportEvent> event = m_transport.poll()) {
        switch (event->kind) {
        case TransportEventKind::Connected: {
            ServerClient client;
            client.peer = event->peer;
            client.input = InputBuffer(m_config.input);
            client.snapshots = SnapshotSender(m_config.snapshotBudgetBytes);
            m_clients.push_back(std::move(client));
            break;
        }
        case TransportEventKind::Disconnected: {
            // Gone with no Bye (one the server closed itself is no longer in the list).
            const std::size_t index = find(event->peer);
            if (index < m_clients.size()) {
                if (m_clients[index].joined) {
                    m_events.push_back(ServerEvent{.kind = ServerEvent::Kind::Left,
                                                   .slot = m_clients[index].slot,
                                                   .name = m_clients[index].name,
                                                   .reason = ByeReason::Quit,
                                                   .connectionLost = true});
                }
                m_clients.erase(m_clients.begin() + static_cast<std::ptrdiff_t>(index));
            }
            break;
        }
        case TransportEventKind::Received: {
            const std::size_t index = find(event->peer);
            if (index >= m_clients.size()) {
                break; // a late packet from a connection already closed
            }
            auto message = decode(event->data);
            if (!message) {
                close(index, ByeReason::ProtocolError, true);
                break;
            }
            m_clients[index].stepsSinceHeard = 0;
            (void)handle(m_clients[index], *message);
            break;
        }
        }
    }

    // This step's timeouts, from the back so closing one leaves the indexes ahead valid.
    for (std::size_t i = m_clients.size(); i-- > 0;) {
        ServerClient& client = m_clients[i];
        ++client.stepsSinceHeard;
        ++client.stepsConnected;
        const bool handshakeOver = !client.joined && client.stepsConnected > m_config.handshakeTimeoutSteps;
        const bool silent = client.joined && client.stepsSinceHeard > m_config.timeoutSteps;
        if (handshakeOver || silent) {
            close(i, ByeReason::TimedOut, true);
        }
    }

    // Each player's pad for this step, from the commands that have arrived.
    for (ServerClient& client : m_clients) {
        if (client.joined) {
            (void)client.input.take();
        }
    }
}

void NetServer::send() {
    if (!m_running) {
        return;
    }
    // Pings on a fixed schedule of steps, to joined players only (a handshaking one has nothing to measure yet).
    if (m_config.pingIntervalSteps > 0 && m_steps % m_config.pingIntervalSteps == 0) {
        for (ServerClient& client : m_clients) {
            if (!client.joined) {
                continue;
            }
            sendTo(client, Ping{.sequence = client.quality.pingSent(m_nowNs)});
            // How early their commands come, once they come at all, so the client can send earlier or later.
            if (client.input.started()) {
                const std::int64_t lead = std::clamp<std::int64_t>(client.input.lead(), -1000, 1000);
                sendTo(client, InputTiming{.lead = static_cast<std::int32_t>(lead),
                                           .target = static_cast<std::uint8_t>(
                                               std::min<std::uint32_t>(m_config.input.delaySteps, 255))});
            }
        }
    }
    ++m_steps;
    m_transport.flush();
}

void NetServer::sendSnapshots(std::uint32_t step, std::span<const SnapshotHuman> humans) {
    if (!m_running) {
        return;
    }
    for (ServerClient& client : m_clients) {
        if (client.joined) {
            sendTo(client, client.snapshots.build(step, humans));
        }
    }
}

std::size_t NetServer::joinedCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_clients, [](const ServerClient& c) { return c.joined; }));
}

std::optional<PadSample> NetServer::remotePad(std::uint8_t slot) const {
    const auto found =
        std::ranges::find_if(m_clients, [slot](const ServerClient& c) { return c.joined && c.slot == slot; });
    if (found == m_clients.end()) {
        return std::nullopt;
    }
    return found->input.current();
}

std::vector<ServerEvent> NetServer::takeEvents() { return std::exchange(m_events, {}); }

bool NetServer::handle(ServerClient& client, const Message& message) {
    const std::size_t index = find(client.peer);
    if (const auto* hello = std::get_if<Hello>(&message)) {
        if (client.joined) {
            close(index, ByeReason::ProtocolError, true); // a second Hello is out of turn
            return false;
        }
        return handleHello(client, *hello);
    }
    if (const auto* ping = std::get_if<Ping>(&message)) {
        sendTo(client, Pong{.sequence = ping->sequence});
        return true;
    }
    if (const auto* pong = std::get_if<Pong>(&message)) {
        client.quality.pongReceived(pong->sequence, m_nowNs);
        return true;
    }
    if (const auto* commands = std::get_if<InputCommands>(&message)) {
        if (!client.joined) {
            close(index, ByeReason::ProtocolError, true); // a pad belongs to a player, and they have not joined
            return false;
        }
        // Unreliable delivery may reorder and repeat; the buffer keeps each step once and plays it on its own step.
        for (std::size_t i = 0; i < commands->pads.size(); ++i) {
            client.input.add(commands->newestStep - static_cast<std::uint32_t>(i), commands->pads[i]);
        }
        return true;
    }
    if (const auto* ack = std::get_if<SnapshotAck>(&message)) {
        // A snapshot goes only to a joined player, and one never sent cannot be acknowledged.
        if (!client.joined || !client.snapshots.acknowledge(ack->sequence).has_value()) {
            close(index, ByeReason::ProtocolError, true);
            return false;
        }
        return true;
    }
    if (std::holds_alternative<Bye>(message)) {
        // The player is leaving: report it, and close without a Bye of our own.
        close(index, std::get<Bye>(message).reason, false);
        return false;
    }
    // Welcome, Reject, InputTiming and Snapshot only ever go from a server to a client.
    close(index, ByeReason::ProtocolError, true);
    return false;
}

bool NetServer::handleHello(ServerClient& client, const Hello& hello) {
    const std::size_t index = find(client.peer);
    // Refusals in order of what the player can do about them: another version, another name, another time.
    std::optional<RejectReason> refusal;
    if (hello.version != kProtocolVersion) {
        refusal = RejectReason::VersionMismatch;
    } else if (!validPlayerName(hello.playerName)) {
        refusal = RejectReason::BadName;
    } else if (joinedCount() >= m_config.maxClients || freeSlot() == 0) {
        refusal = RejectReason::ServerFull;
    }
    if (refusal) {
        sendTo(client, Reject{.reason = *refusal, .serverVersion = kProtocolVersion});
        close(index, ByeReason::Quit, false); // the Reject says why; no Bye after it
        return false;
    }
    client.slot = freeSlot();
    client.name = hello.playerName;
    client.joined = true;
    client.stepsSinceHeard = 0;
    sendTo(client,
           Welcome{.playerSlot = client.slot, .stepRateHz = m_config.stepRateHz, .serverName = m_config.serverName});
    m_events.push_back(ServerEvent{.kind = ServerEvent::Kind::Joined,
                                   .slot = client.slot,
                                   .name = client.name,
                                   .reason = ByeReason::Quit,
                                   .connectionLost = false});
    return true;
}

void NetServer::sendTo(const ServerClient& client, const Message& message) {
    if (auto sent = m_transport.send(client.peer, deliveryOf(message), encode(message)); !sent) {
        return; // the connection is gone; the transport reports it
    }
}

void NetServer::close(std::size_t index, ByeReason reason, bool bye) {
    const ServerClient& client = m_clients[index];
    if (bye) {
        sendTo(client, Bye{.reason = reason});
    }
    if (client.joined) {
        m_events.push_back(ServerEvent{.kind = ServerEvent::Kind::Left,
                                       .slot = client.slot,
                                       .name = client.name,
                                       .reason = reason,
                                       .connectionLost = false});
    }
    // The transport sends the reliable Bye (and a Reject) before it closes.
    m_transport.disconnect(client.peer);
    m_clients.erase(m_clients.begin() + static_cast<std::ptrdiff_t>(index));
}

std::uint8_t NetServer::freeSlot() const {
    for (std::size_t slot = 1; slot <= m_config.maxClients && slot <= 255; ++slot) {
        const bool taken = std::ranges::any_of(
            m_clients, [&](const ServerClient& c) { return c.joined && c.slot == static_cast<std::uint8_t>(slot); });
        if (!taken) {
            return static_cast<std::uint8_t>(slot);
        }
    }
    return 0;
}

std::size_t NetServer::find(PeerId peer) const {
    const auto found = std::ranges::find(m_clients, peer, &ServerClient::peer);
    return static_cast<std::size_t>(found - m_clients.begin());
}

} // namespace coney::net
