// SPDX-License-Identifier: GPL-3.0-or-later
// The in-memory loopback transport (docs/guides/multiplayer.md#transport): connections between transports on one
// LoopbackNetwork, packets in order, closes seen on both ends, refusals, and free ports.
#include "net/loopback_transport.h"

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "net/transport.h"
#include "support/net_fixtures.h"

using coney::ErrorCode;
using coney::net::Delivery;
using coney::net::ListenConfig;
using coney::net::LoopbackNetwork;
using coney::net::LoopbackTransport;
using coney::net::TransportEventKind;
using coney::net::test::drain;
using coney::net::test::packet;
using coney::net::test::text;

TEST_CASE("a client connects to a listening loopback and both ends see Connected", "[net][loopback]") {
    LoopbackNetwork network;
    LoopbackTransport server(network, "host");
    LoopbackTransport client(network, "guest");
    REQUIRE(server.listen(ListenConfig{.port = 27615, .maxPeers = 2, .bindAddress = {}}).has_value());
    CHECK(server.boundPort() == 27615);
    const auto peer = client.connect("host", 27615);
    REQUIRE(peer.has_value());

    const auto clientEvents = drain(client);
    REQUIRE(clientEvents.size() == 1);
    CHECK(clientEvents[0].kind == TransportEventKind::Connected);
    CHECK(clientEvents[0].peer == *peer);
    const auto serverEvents = drain(server);
    REQUIRE(serverEvents.size() == 1);
    CHECK(serverEvents[0].kind == TransportEventKind::Connected);
}

TEST_CASE("loopback packets arrive in send order, whatever their delivery", "[net][loopback]") {
    LoopbackNetwork network;
    LoopbackTransport server(network, "host");
    LoopbackTransport client(network, "guest");
    REQUIRE(server.listen(ListenConfig{.port = 0, .maxPeers = 1, .bindAddress = {}}).has_value());
    const auto peer = client.connect("host", server.boundPort()).value();
    const auto serverPeer = drain(server).at(0).peer;
    (void)drain(client);

    REQUIRE(client.send(peer, Delivery::ReliableOrdered, packet("one")).has_value());
    REQUIRE(client.send(peer, Delivery::UnreliableSequenced, packet("two")).has_value());
    REQUIRE(client.send(peer, Delivery::Unreliable, packet("three")).has_value());
    client.flush();
    const auto received = drain(server);
    REQUIRE(received.size() == 3);
    CHECK(text(received[0].data) == "one");
    CHECK(text(received[1].data) == "two");
    CHECK(text(received[2].data) == "three");
    CHECK(received[0].peer == serverPeer);

    // And back the other way.
    REQUIRE(server.send(serverPeer, Delivery::ReliableOrdered, packet("reply")).has_value());
    const auto replies = drain(client);
    REQUIRE(replies.size() == 1);
    CHECK(text(replies[0].data) == "reply");

    CHECK(client.stats().packetsSent == 3);
    CHECK(client.stats().bytesSent == 11);
    CHECK(server.stats().packetsReceived == 3);
    CHECK(client.stats().bytesReceived == 5);
}

TEST_CASE("a close is seen on both ends, after the packets sent before it", "[net][loopback]") {
    LoopbackNetwork network;
    LoopbackTransport server(network, "host");
    LoopbackTransport client(network, "guest");
    REQUIRE(server.listen(ListenConfig{.port = 0, .maxPeers = 1, .bindAddress = {}}).has_value());
    const auto peer = client.connect("host", server.boundPort()).value();
    (void)drain(server);
    (void)drain(client);

    REQUIRE(client.send(peer, Delivery::ReliableOrdered, packet("bye")).has_value());
    client.disconnect(peer);
    const auto serverEvents = drain(server);
    REQUIRE(serverEvents.size() == 2);
    CHECK(serverEvents[0].kind == TransportEventKind::Received);
    CHECK(serverEvents[1].kind == TransportEventKind::Disconnected);
    const auto clientEvents = drain(client);
    REQUIRE(clientEvents.size() == 1);
    CHECK(clientEvents[0].kind == TransportEventKind::Disconnected);

    // The id is gone for good.
    const auto late = client.send(peer, Delivery::ReliableOrdered, packet("late"));
    REQUIRE_FALSE(late.has_value());
    CHECK(late.error().code == ErrorCode::NotFound);
}

TEST_CASE("connecting where nobody listens, or to a full listener, fails as Disconnected", "[net][loopback]") {
    LoopbackNetwork network;
    LoopbackTransport lonely(network, "guest");
    const auto peer = lonely.connect("nobody", 1).value();
    const auto events = drain(lonely);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == TransportEventKind::Disconnected);
    CHECK(events[0].peer == peer);

    LoopbackTransport server(network, "host");
    REQUIRE(server.listen(ListenConfig{.port = 0, .maxPeers = 1, .bindAddress = {}}).has_value());
    LoopbackTransport first(network, "a");
    LoopbackTransport second(network, "b");
    (void)first.connect("host", server.boundPort()).value();
    (void)second.connect("host", server.boundPort()).value();
    CHECK(drain(first).at(0).kind == TransportEventKind::Connected);
    CHECK(drain(second).at(0).kind == TransportEventKind::Disconnected);
}

TEST_CASE("a transport going away closes its connections at the far end", "[net][loopback]") {
    LoopbackNetwork network;
    LoopbackTransport server(network, "host");
    REQUIRE(server.listen(ListenConfig{.port = 0, .maxPeers = 4, .bindAddress = {}}).has_value());
    {
        LoopbackTransport client(network, "guest");
        (void)client.connect("host", server.boundPort()).value();
        CHECK(drain(server).size() == 1);
    }
    const auto events = drain(server);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == TransportEventKind::Disconnected);
}

TEST_CASE("port 0 gets distinct free ports, and a taken port or a second listen is refused", "[net][loopback]") {
    LoopbackNetwork network;
    LoopbackTransport a(network, "host");
    LoopbackTransport b(network, "host");
    LoopbackTransport c(network, "host");
    REQUIRE(a.listen(ListenConfig{.port = 0, .maxPeers = 1, .bindAddress = {}}).has_value());
    REQUIRE(b.listen(ListenConfig{.port = 0, .maxPeers = 1, .bindAddress = {}}).has_value());
    CHECK(a.boundPort() != 0);
    CHECK(a.boundPort() != b.boundPort());
    const auto taken = c.listen(ListenConfig{.port = a.boundPort(), .maxPeers = 1, .bindAddress = {}});
    REQUIRE_FALSE(taken.has_value());
    CHECK(taken.error().code == ErrorCode::PlatformFailure);
    CHECK_FALSE(a.listen(ListenConfig{.port = 0, .maxPeers = 1, .bindAddress = {}}).has_value());
    CHECK_FALSE(a.connect("host", b.boundPort()).has_value());
}
