// SPDX-License-Identifier: GPL-3.0-or-later
// The ENet transport on this machine's own address (docs/guides/multiplayer.md#enet): a connection on a free port of
// 127.0.0.1, packets of every delivery kind including one big enough to fragment, a close, and the sessions'
// handshake over real UDP. Listening on 127.0.0.1 only keeps a desktop firewall from asking about the test.
#include "platform/enet_transport.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "net/net_client.h"
#include "net/net_server.h"
#include "net/transport.h"
#include "support/net_fixtures.h"

using coney::net::ClientConfig;
using coney::net::ClientState;
using coney::net::Delivery;
using coney::net::ListenConfig;
using coney::net::NetClient;
using coney::net::NetServer;
using coney::net::PeerId;
using coney::net::ServerConfig;
using coney::net::TransportEvent;
using coney::net::TransportEventKind;
using coney::net::test::packet;
using coney::net::test::text;
using coney::platform::EnetTransport;

namespace {

// Waits (in short ENet waits, at most about four seconds) for the next event on `transport`. When none comes it fails a
// check and returns a Received event from peer 0, which no connection has, so the test's own checks fail too.
TransportEvent next(EnetTransport& transport, EnetTransport& other) {
    for (int i = 0; i < 400; ++i) {
        if (auto event = transport.poll()) {
            return std::move(*event);
        }
        // The other end must be serviced too, or its acknowledgements and answers never go out; its events stay queued.
        (void)other.waitForEvent(0);
        (void)transport.waitForEvent(10);
    }
    FAIL_CHECK("no network event within about four seconds");
    return TransportEvent{.kind = TransportEventKind::Received, .peer = 0, .data = {}};
}

} // namespace

TEST_CASE("ENet connects on 127.0.0.1, carries every delivery kind and closes", "[net][enet]") {
    EnetTransport server;
    EnetTransport client;
    REQUIRE(server.listen(ListenConfig{.port = 0, .maxPeers = 2, .bindAddress = "127.0.0.1"}).has_value());
    REQUIRE(server.boundPort() != 0);
    const auto peer = client.connect("127.0.0.1", server.boundPort());
    REQUIRE(peer.has_value());

    // Both ends see the connection.
    const auto clientUp = next(client, server);
    CHECK(clientUp.kind == TransportEventKind::Connected);
    CHECK(clientUp.peer == *peer);
    const auto serverUp = next(server, client);
    REQUIRE(serverUp.kind == TransportEventKind::Connected);
    const PeerId serverPeer = serverUp.peer;

    // One packet of each kind, and an unreliable one far over one datagram (fragmented, without resends).
    const std::string big(4000, 'b');
    REQUIRE(client.send(*peer, Delivery::ReliableOrdered, packet("reliable")).has_value());
    REQUIRE(client.send(*peer, Delivery::UnreliableSequenced, packet("sequenced")).has_value());
    REQUIRE(client.send(*peer, Delivery::Unreliable, packet("unsequenced")).has_value());
    REQUIRE(client.send(*peer, Delivery::UnreliableSequenced, packet(big)).has_value());
    client.flush();
    std::vector<std::string> received;
    while (received.size() < 4) {
        const auto event = next(server, client);
        REQUIRE(event.kind == TransportEventKind::Received);
        CHECK(event.peer == serverPeer);
        received.push_back(text(event.data));
    }
    // Different channels may interleave; every packet arrives whole on a local link.
    CHECK(std::ranges::count(received, std::string("reliable")) == 1);
    CHECK(std::ranges::count(received, std::string("sequenced")) == 1);
    CHECK(std::ranges::count(received, std::string("unsequenced")) == 1);
    CHECK(std::ranges::count(received, big) == 1);
    CHECK(client.stats().packetsSent == 4);
    CHECK(server.stats().bytesReceived == 8 + 9 + 11 + 4000);

    // A close reaches the far end, and then this end.
    client.disconnect(*peer);
    client.flush();
    const auto down = next(server, client);
    CHECK(down.kind == TransportEventKind::Disconnected);
    const auto closed = next(client, server);
    CHECK(closed.kind == TransportEventKind::Disconnected);
    CHECK_FALSE(client.send(*peer, Delivery::ReliableOrdered, packet("late")).has_value());
}

TEST_CASE("ENet's Bye of a closing transport arrives before the close once it reports closed", "[net][enet]") {
    // ENet throws away a peer's undelivered packets when that peer's close arrives: a transport destroyed right behind
    // its last reliable packet can lose it. One kept going until closed() delivers it first.
    EnetTransport server;
    REQUIRE(server.listen(ListenConfig{.port = 0, .maxPeers = 2, .bindAddress = "127.0.0.1"}).has_value());
    std::optional<EnetTransport> client(std::in_place);
    const auto peer = client->connect("127.0.0.1", server.boundPort());
    REQUIRE(peer.has_value());
    REQUIRE(next(*client, server).kind == TransportEventKind::Connected);
    REQUIRE(next(server, *client).kind == TransportEventKind::Connected);
    CHECK_FALSE(client->closed());

    REQUIRE(client->send(*peer, Delivery::ReliableOrdered, packet("bye")).has_value());
    client->disconnect(*peer);
    client->flush();
    std::vector<TransportEvent> events;
    for (int i = 0; i < 400 && !client->closed(); ++i) {
        (void)client->waitForEvent(5);
        while (client->poll()) {
        }
        (void)server.waitForEvent(5);
        while (auto event = server.poll()) {
            events.push_back(std::move(*event));
        }
    }
    REQUIRE(client->closed());
    client.reset();
    for (int i = 0; i < 400 && (events.empty() || events.back().kind != TransportEventKind::Disconnected); ++i) {
        (void)server.waitForEvent(10);
        while (auto event = server.poll()) {
            events.push_back(std::move(*event));
        }
    }
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == TransportEventKind::Received);
    CHECK(text(events[0].data) == "bye");
    CHECK(events[1].kind == TransportEventKind::Disconnected);
}

TEST_CASE("ENet refuses an address that does not resolve and a second listen", "[net][enet]") {
    EnetTransport transport;
    CHECK_FALSE(transport.connect("", 1).has_value());
    EnetTransport server;
    REQUIRE(server.listen(ListenConfig{.port = 0, .maxPeers = 1, .bindAddress = "127.0.0.1"}).has_value());
    CHECK_FALSE(server.listen(ListenConfig{.port = 0, .maxPeers = 1, .bindAddress = "127.0.0.1"}).has_value());
    CHECK_FALSE(server.connect("127.0.0.1", 1).has_value());
}

TEST_CASE("the sessions' handshake runs over ENet", "[net][enet]") {
    EnetTransport serverWire;
    EnetTransport clientWire;
    ServerConfig serverConfig;
    serverConfig.port = 0;
    serverConfig.bindAddress = "127.0.0.1";
    NetServer server(serverWire, serverConfig);
    REQUIRE(server.start().has_value());
    ClientConfig clientConfig;
    clientConfig.playerName = "Fox";
    NetClient client(clientWire, clientConfig);
    REQUIRE(client.connect("127.0.0.1", server.port()).has_value());

    // Steps with short waits for traffic in between, until the client has joined (well inside the timeouts).
    for (int i = 0; i < 200 && client.state() != ClientState::Connected; ++i) {
        (void)serverWire.waitForEvent(5);
        (void)clientWire.waitForEvent(5);
        server.receive();
        client.receive();
        server.send();
        client.send();
    }
    REQUIRE(client.state() == ClientState::Connected);
    CHECK(client.playerSlot() == 1);
    const auto events = server.takeEvents();
    REQUIRE(events.size() == 1);
    CHECK(events[0].name == "Fox");
}
