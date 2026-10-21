// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/net_play.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/options.h"
#include "core/pad.h"
#include "core/pads.h"
#include "net/link_conditioner.h"
#include "net/net_client.h"
#include "net/net_server.h"
#include "net/net_status.h"
#include "net/protocol.h"
#include "net/transport.h"
#include "platform/enet_transport.h"

namespace coney::platform {

// The command line's default port is the protocol's; core cannot include the network library to share the constant.
static_assert(kDefaultNetPort == net::kDefaultPort);

namespace {

// The host's remote player plays pad port 2; the console has no third (docs/research/frontend.md#pad-record).
constexpr std::size_t kRemotePort = 1;
constexpr std::uint8_t kRemoteSlot = 1;

} // namespace

std::unique_ptr<net::Transport> makeEnetTransport() { return std::make_unique<EnetTransport>(); }

PortSamples NetInput::sample(std::uint64_t frame) {
    const PortSamples local = m_inner != nullptr ? m_inner->sample(frame) : PortSamples{};
    return m_net.pads(local);
}

NetPlay::NetPlay(TransportFactory factory, std::function<void(std::string_view)> print)
    : m_factory(std::move(factory)), m_print(std::move(print)), m_port(net::kDefaultPort) {}

// NOLINTNEXTLINE(bugprone-exception-escape): leaving can only throw on allocation, which ends the program anyway
NetPlay::~NetPlay() { disconnect(); }

void NetPlay::note(const std::string& line) {
    m_log.push_back(line);
    while (m_log.size() > kLogLines) {
        m_log.pop_front();
    }
    if (m_print) {
        m_print(std::format("net: {}\n", line));
    }
}

net::LinkConditions NetPlay::conditions() const {
    net::LinkConditions conditions;
    conditions.latencyMs = m_simulation.latencyMs;
    conditions.jitterMs = m_simulation.jitterMs;
    conditions.lossFraction = m_simulation.lossPercent / 100.0F;
    conditions.duplicateFraction = m_simulation.duplicatePercent / 100.0F;
    conditions.reorderFraction = m_simulation.reorderPercent / 100.0F;
    return conditions;
}

bool NetPlay::setPlayerName(const std::string& name) {
    if (!net::validPlayerName(name)) {
        note(std::format("\"{}\" is not a player name: 1 to 32 printable ASCII characters", name));
        return false;
    }
    m_playerName = name;
    return true;
}

void NetPlay::setSimulation(const net::NetSimulation& simulation) {
    m_simulation = simulation;
    if (m_link) {
        m_link->setConditions(conditions());
    }
}

bool NetPlay::hostOn(const std::string& bindAddress) {
    disconnect();
    // A host that is still closing on this port holds its socket: close it at once so the port can be bound again.
    std::erase_if(m_closing,
                  [this](const Closing& closing) { return m_port != 0 && closing.link->boundPort() == m_port; });
    // A fresh transport per session: a stopped server's socket is closed with its transport.
    m_wire = m_factory();
    m_link = std::make_unique<net::LinkConditioner>(*m_wire);
    m_link->setConditions(conditions());
    net::ServerConfig config;
    config.port = m_port;
    config.bindAddress = bindAddress;
    config.serverName = m_playerName;
    // One remote player: they are the host's pad port 2, and the game has two players at most, as the original's two
    // ports give (only pad records 0 and 4 are read, 0x001454a8).
    config.maxClients = kPadPorts - 1;
    m_server = std::make_unique<net::NetServer>(*m_link, config);
    if (auto started = m_server->start(); !started) {
        note(std::format("cannot host on port {}: {}", m_port, started.error().message));
        teardown();
        return false;
    }
    note(std::format("hosting on port {} as {}", m_server->port(), m_playerName));
    return true;
}

bool NetPlay::join() {
    disconnect();
    // ADDR or ADDR:PORT; without a port, the page's.
    std::string host = m_address;
    std::uint16_t port = m_port;
    if (const auto colon = m_address.rfind(':'); colon != std::string::npos) {
        host = m_address.substr(0, colon);
        const std::string_view portText = std::string_view(m_address).substr(colon + 1);
        unsigned value = 0;
        const auto parsed = std::from_chars(portText.data(), portText.data() + portText.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != portText.data() + portText.size() || value == 0 ||
            value > 65535) {
            note(std::format("\"{}\" has no valid port after the colon", m_address));
            return false;
        }
        port = static_cast<std::uint16_t>(value);
    }
    if (host.empty() || port == 0) {
        note("joining needs a server address and a port");
        return false;
    }
    m_wire = m_factory();
    m_link = std::make_unique<net::LinkConditioner>(*m_wire);
    m_link->setConditions(conditions());
    net::ClientConfig config;
    config.playerName = m_playerName;
    m_client = std::make_unique<net::NetClient>(*m_link, config);
    if (auto connecting = m_client->connect(host, port); !connecting) {
        note(std::format("cannot join {}:{}: {}", host, port, connecting.error().message));
        teardown();
        return false;
    }
    m_lastClientState = m_client->describe();
    note(std::format("joining {}:{} as {}", host, port, m_playerName));
    return true;
}

void NetPlay::disconnect() {
    if (m_server) {
        const std::size_t players = m_server->joinedCount();
        m_server->stop();
        note(std::format("stopped hosting ({} player{} told)", players, players == 1 ? "" : "s"));
    }
    if (m_client) {
        const bool live = m_client->state() == net::ClientState::Connecting ||
                          m_client->state() == net::ClientState::Handshaking ||
                          m_client->state() == net::ClientState::Connected;
        m_client->disconnect();
        if (live) {
            note("left the game");
        }
    }
    teardown();
}

void NetPlay::teardown() {
    m_server.reset();
    m_client.reset();
    if (m_link) {
        if (m_link->closed()) {
            m_link.reset();
            m_wire.reset();
        } else {
            m_closing.push_back(
                Closing{.wire = std::move(m_wire), .link = std::move(m_link), .stepsLeft = kCloseSteps});
        }
    }
    m_link.reset();
    m_wire.reset();
}

void NetPlay::serviceClosing(std::uint64_t elapsedNs, bool countStep) {
    for (Closing& closing : m_closing) {
        closing.link->advance(elapsedNs);
        while (closing.link->poll()) {
            // A closing transport's events matter to nobody; reading them is what moves its connections along.
        }
        closing.link->flush();
        if (countStep && closing.stepsLeft > 0) {
            --closing.stepsLeft;
        }
    }
    std::erase_if(m_closing, [](const Closing& closing) { return closing.link->closed() || closing.stepsLeft == 0; });
}

void NetPlay::shutdown(const std::function<std::uint64_t()>& wait) {
    disconnect();
    // A backstop on the calls as well as the time, in case `wait` reports no time passing.
    constexpr int kMaxWaits = 1000;
    std::uint64_t waited = 0;
    for (int calls = 0; !m_closing.empty() && waited < kQuitWaitNs && calls < kMaxWaits; ++calls) {
        const std::uint64_t elapsedNs = wait();
        waited += elapsedNs;
        serviceClosing(elapsedNs, false);
    }
    m_closing.clear();
}

void NetPlay::advance(std::uint64_t elapsedNs) {
    m_closingElapsedNs += elapsedNs;
    if (m_server) {
        m_server->advance(elapsedNs);
    } else if (m_client) {
        m_client->advance(elapsedNs);
    }
}

void NetPlay::beforeStep() {
    if (m_server) {
        m_server->receive();
    } else if (m_client) {
        m_client->receive();
    }
}

void NetPlay::afterStep() {
    if (!m_closing.empty()) {
        serviceClosing(m_closingElapsedNs, true);
    }
    m_closingElapsedNs = 0;
    if (m_server) {
        m_server->send();
        for (const net::ServerEvent& event : m_server->takeEvents()) {
            if (event.kind == net::ServerEvent::Kind::Joined) {
                note(std::format("{} joined as player {} on pad port {}", event.name, event.slot + 1, kRemotePort + 1));
            } else {
                note(std::format("{} (player {}) {}", event.name, event.slot + 1,
                                 event.connectionLost ? std::string_view("lost the connection")
                                                      : net::describe(event.reason)));
            }
        }
    } else if (m_client) {
        m_client->send();
        // The client's state changes, once each.
        if (std::string state = m_client->describe(); state != m_lastClientState) {
            m_lastClientState = state;
            note(state);
        }
    }
}

PortSamples NetPlay::pads(const PortSamples& local) {
    PortSamples samples = local;
    if (m_server) {
        if (const std::optional<PadSample> remote = m_server->remotePad(kRemoteSlot); remote) {
            samples.at(kRemotePort) = *remote;
        }
    } else if (m_client) {
        m_client->setPad(local.at(0));
    }
    return samples;
}

net::NetStatus NetPlay::status() const {
    net::NetStatus status;
    if (m_wire) {
        const net::TransportStats stats = m_wire->stats();
        status.bytesSent = stats.bytesSent;
        status.bytesReceived = stats.bytesReceived;
        status.packetsSent = stats.packetsSent;
        status.packetsReceived = stats.packetsReceived;
    }
    if (m_server) {
        status.role = net::NetRole::Host;
        status.port = m_server->port();
        const std::size_t joined = m_server->joinedCount();
        status.state =
            std::format("hosting on port {}, {} player{} joined", status.port, joined, joined == 1 ? "" : "s");
        for (const net::ServerClient& client : m_server->clients()) {
            if (!client.joined) {
                continue;
            }
            status.players.push_back(net::NetPlayerStatus{.slot = client.slot,
                                                          .name = client.name,
                                                          .roundTripNs = client.quality.roundTripNs(),
                                                          .lossFraction = client.quality.lossFraction()});
            // The host's figures are its slowest player's: the one a problem shows on first.
            status.roundTripNs = std::max(status.roundTripNs, client.quality.roundTripNs());
            status.lossFraction = std::max(status.lossFraction, client.quality.lossFraction());
        }
    } else if (m_client) {
        const bool ended = m_client->state() == net::ClientState::Disconnected;
        status.role = ended ? net::NetRole::Offline : net::NetRole::Client;
        status.state = m_client->describe();
        status.roundTripNs = m_client->quality().roundTripNs();
        status.lossFraction = m_client->quality().lossFraction();
    }
    return status;
}

std::string NetPlay::summary() const {
    const net::NetStatus now = status();
    return std::format("net: {}; {} B sent, {} B received\n", now.state, now.bytesSent, now.bytesReceived);
}

} // namespace coney::platform
