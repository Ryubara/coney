// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/net_client.h"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "core/error.h"
#include "core/pad.h"
#include "net/protocol.h"
#include "net/snapshot.h"
#include "net/transport.h"

namespace coney::net {

NetClient::NetClient(Transport& transport, ClientConfig config) : m_transport(transport), m_config(std::move(config)) {}

// NOLINTNEXTLINE(bugprone-exception-escape): disconnect() can only throw on allocation, which ends the program anyway
NetClient::~NetClient() { disconnect(); }

std::expected<void, Error> NetClient::connect(std::string_view address, std::uint16_t port) {
    if (m_state == ClientState::Connecting || m_state == ClientState::Handshaking ||
        m_state == ClientState::Connected) {
        return fail(ErrorCode::InvalidArgument, "the client is already connecting or connected");
    }
    if (m_config.inputSteps == 0 || m_config.inputSteps > kMaxInputCommands) {
        return fail(ErrorCode::InvalidArgument, "a client sends 1 to 8 steps of input commands");
    }
    auto peer = m_transport.connect(address, port);
    if (!peer) {
        return std::unexpected(peer.error());
    }
    // A fresh session: nothing of the last one carries over.
    m_peer = *peer;
    m_state = ClientState::Connecting;
    m_end = ClientEnd::None;
    m_rejectReason.reset();
    m_byeReason.reset();
    m_welcome = Welcome{};
    m_quality = LinkQuality{};
    m_stepsInState = 0;
    m_stepsSinceHeard = 0;
    m_steps = 0;
    m_snapshots = SnapshotReceiver{};
    m_ackDue = 0;
    return {};
}

void NetClient::disconnect() {
    if (m_state == ClientState::Connecting) {
        finish(ClientEnd::Left, false);
    } else if (m_state == ClientState::Handshaking || m_state == ClientState::Connected) {
        finish(ClientEnd::Left, true, ByeReason::Quit);
        m_transport.flush();
    }
}

void NetClient::advance(std::uint64_t elapsedNs) {
    m_nowNs += elapsedNs;
    m_transport.advance(elapsedNs);
}

void NetClient::receive() {
    // Events are drained in every state, so a closed connection's leftovers never pile up in the transport.
    while (std::optional<TransportEvent> event = m_transport.poll()) {
        if (event->peer != m_peer || m_state == ClientState::Idle || m_state == ClientState::Disconnected) {
            continue; // a previous session's connection
        }
        switch (event->kind) {
        case TransportEventKind::Connected:
            if (m_state == ClientState::Connecting) {
                m_state = ClientState::Handshaking;
                m_stepsInState = 0;
                sendToServer(Hello{.version = kProtocolVersion, .playerName = m_config.playerName});
            }
            break;
        case TransportEventKind::Disconnected:
            m_peer = 0; // already closed underneath: finish() must not close it again
            finish(m_state == ClientState::Connecting ? ClientEnd::Unreachable : ClientEnd::ConnectionLost, false);
            break;
        case TransportEventKind::Received: {
            auto message = decode(event->data);
            if (!message) {
                finish(ClientEnd::ProtocolError, true, ByeReason::ProtocolError);
                break;
            }
            m_stepsSinceHeard = 0;
            handle(*message);
            break;
        }
        }
    }

    // This step's timeouts.
    ++m_stepsInState;
    ++m_stepsSinceHeard;
    if ((m_state == ClientState::Connecting || m_state == ClientState::Handshaking) &&
        m_stepsInState > m_config.connectTimeoutSteps) {
        finish(ClientEnd::TimedOut, m_state == ClientState::Handshaking, ByeReason::TimedOut);
    } else if (m_state == ClientState::Connected && m_stepsSinceHeard > m_config.timeoutSteps) {
        finish(ClientEnd::TimedOut, true, ByeReason::TimedOut);
    }
}

void NetClient::send() {
    if (m_state == ClientState::Connected) {
        // This step's pad and the latest before it, numbered by the step so the server plays each on a step of its
        // own and a lost packet's steps come in the next. 32 bits of steps is four and a half years at 30 Hz.
        m_sent.at(m_steps % kMaxInputCommands) = m_pad;
        const std::uint64_t count = std::min<std::uint64_t>(m_config.inputSteps, m_steps + 1);
        InputCommands commands{.newestStep = static_cast<std::uint32_t>(m_steps), .pads = {}};
        for (std::uint64_t i = 0; i < count; ++i) {
            commands.pads.push_back(m_sent.at((m_steps - i) % kMaxInputCommands));
        }
        sendToServer(commands);
        // The newest snapshot decoded, so the server's next ones are deltas against it; one lost is replaced by the
        // next step's.
        if (m_ackDue != 0) {
            sendToServer(SnapshotAck{.sequence = std::exchange(m_ackDue, 0)});
        }
        if (m_config.pingIntervalSteps > 0 && m_steps % m_config.pingIntervalSteps == 0) {
            sendToServer(Ping{.sequence = m_quality.pingSent(m_nowNs)});
        }
        ++m_steps;
    }
    m_transport.flush();
}

void NetClient::handle(const Message& message) {
    if (const auto* welcome = std::get_if<Welcome>(&message); welcome != nullptr) {
        if (m_state != ClientState::Handshaking) {
            finish(ClientEnd::ProtocolError, true, ByeReason::ProtocolError);
            return;
        }
        m_welcome = *welcome;
        m_timing.reset();
        m_state = ClientState::Connected;
        m_stepsInState = 0;
        m_steps = 0;
    } else if (const auto* reject = std::get_if<Reject>(&message); reject != nullptr) {
        m_rejectReason = reject->reason;
        finish(ClientEnd::Rejected, false);
    } else if (const auto* bye = std::get_if<Bye>(&message); bye != nullptr) {
        m_byeReason = bye->reason;
        finish(ClientEnd::ServerClosed, false);
    } else if (const auto* ping = std::get_if<Ping>(&message); ping != nullptr) {
        sendToServer(Pong{.sequence = ping->sequence});
    } else if (const auto* pong = std::get_if<Pong>(&message); pong != nullptr) {
        m_quality.pongReceived(pong->sequence, m_nowNs);
    } else if (const auto* timing = std::get_if<InputTiming>(&message); timing != nullptr) {
        // Unreliable and on another channel than Welcome, so it may overtake it: kept only once joined.
        if (m_state == ClientState::Connected) {
            m_timing = *timing;
        }
    } else if (const auto* snapshot = std::get_if<Snapshot>(&message); snapshot != nullptr) {
        // Unreliable and on another channel than Welcome, so it may overtake it: decoded only once joined.
        if (m_state != ClientState::Connected) {
            return;
        }
        const auto applied = m_snapshots.apply(*snapshot);
        if (!applied) {
            finish(ClientEnd::ProtocolError, true, ByeReason::ProtocolError);
            return;
        }
        if (*applied == SnapshotApplied::Applied) {
            m_ackDue = m_snapshots.newest();
        }
    } else {
        // A Hello, an InputCommands or a SnapshotAck only ever goes from a client to a server.
        finish(ClientEnd::ProtocolError, true, ByeReason::ProtocolError);
    }
}

std::uint32_t NetClient::clockSpeedPercent() const {
    if (m_state != ClientState::Connected || !m_timing) {
        return 100;
    }
    const std::uint32_t nudge = std::min<std::uint32_t>(m_config.clockNudgePercent, 50);
    // A band of two steps (the target and one over) where nothing changes, so the speed does not flip every Ping.
    if (m_timing->lead < m_timing->target) {
        return 100 + nudge;
    }
    if (m_timing->lead > m_timing->target + 1) {
        return 100 - nudge;
    }
    return 100;
}

void NetClient::finish(ClientEnd end, bool bye, ByeReason reason) {
    if (bye) {
        sendToServer(Bye{.reason = reason});
    }
    if (m_peer != 0) {
        m_transport.disconnect(m_peer);
    }
    m_state = ClientState::Disconnected;
    m_end = end;
    m_stepsInState = 0;
}

void NetClient::sendToServer(const Message& message) {
    if (m_peer == 0) {
        return;
    }
    if (auto sent = m_transport.send(m_peer, deliveryOf(message), encode(message)); !sent) {
        return; // the connection is gone; the transport reports it
    }
}

std::string NetClient::describe() const {
    switch (m_state) {
    case ClientState::Idle:
        return "not connected";
    case ClientState::Connecting:
        return "connecting";
    case ClientState::Handshaking:
        return "joining";
    case ClientState::Connected:
        return "connected to " + m_welcome.serverName + " as player " + std::to_string(m_welcome.playerSlot + 1);
    case ClientState::Disconnected:
        break;
    }
    switch (m_end) {
    case ClientEnd::None:
    case ClientEnd::Left:
        return "disconnected";
    case ClientEnd::Unreachable:
        return "could not reach the server";
    case ClientEnd::TimedOut:
        return "timed out";
    case ClientEnd::Rejected:
        return "refused: " + std::string(m_rejectReason ? net::describe(*m_rejectReason) : "no reason");
    case ClientEnd::ServerClosed:
        return "the server closed the connection (" +
               std::string(m_byeReason ? net::describe(*m_byeReason) : "no reason") + ")";
    case ClientEnd::ConnectionLost:
        return "connection lost";
    case ClientEnd::ProtocolError:
        return "protocol error";
    }
    return "disconnected";
}

} // namespace coney::net
