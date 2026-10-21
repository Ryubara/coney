// SPDX-License-Identifier: GPL-3.0-or-later
// The link conditioner (docs/guides/multiplayer.md#link-conditioner): latency, jitter, loss, duplicates and reordering
// in front of the loopback, with the delivery contracts kept, and the same draws on every run.
#include "net/link_conditioner.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "net/loopback_transport.h"
#include "net/transport.h"
#include "support/net_fixtures.h"

using coney::net::Delivery;
using coney::net::LinkConditioner;
using coney::net::LinkConditions;
using coney::net::ListenConfig;
using coney::net::LoopbackNetwork;
using coney::net::LoopbackTransport;
using coney::net::PeerId;
using coney::net::TransportEvent;
using coney::net::TransportEventKind;
using coney::net::test::drain;
using coney::net::test::packet;
using coney::net::test::text;

namespace {

constexpr std::uint64_t kMs = 1'000'000;

// A server on the plain loopback and a client whose sending goes through a conditioner, connected.
struct ConditionedLink {
    LoopbackNetwork network;
    LoopbackTransport server{network, "host"};
    LoopbackTransport clientWire{network, "guest"};
    LinkConditioner client{clientWire, 1234};
    PeerId peer = 0;

    ConditionedLink() {
        REQUIRE(server.listen(ListenConfig{.port = 0, .maxPeers = 1, .bindAddress = {}}).has_value());
        peer = client.connect("host", server.boundPort()).value();
        (void)drain(client); // the conditioner learns the connection is up
        (void)drain(server);
    }

    // The packets the server has received, as text.
    std::vector<std::string> received() {
        std::vector<std::string> out;
        for (const TransportEvent& event : drain(server)) {
            if (event.kind == TransportEventKind::Received) {
                out.push_back(text(event.data));
            }
        }
        return out;
    }
};

} // namespace

TEST_CASE("latency holds a packet back until that much time has been advanced", "[net][link_conditioner]") {
    ConditionedLink link;
    link.client.setConditions(LinkConditions{.latencyMs = 100});
    REQUIRE(link.client.send(link.peer, Delivery::Unreliable, packet("late")).has_value());
    link.client.flush();
    CHECK(link.received().empty());
    CHECK(link.client.heldPackets() == 1);
    link.client.advance(99 * kMs);
    CHECK(link.received().empty());
    link.client.advance(1 * kMs);
    CHECK(link.received() == std::vector<std::string>{"late"});
}

TEST_CASE("a perfect link passes packets at the next flush", "[net][link_conditioner]") {
    ConditionedLink link;
    CHECK(link.client.conditions().perfect());
    REQUIRE(link.client.send(link.peer, Delivery::ReliableOrdered, packet("now")).has_value());
    link.client.flush();
    CHECK(link.received() == std::vector<std::string>{"now"});
}

TEST_CASE("reliable packets under jitter and loss arrive once each and in order", "[net][link_conditioner]") {
    ConditionedLink link;
    link.client.setConditions(LinkConditions{
        .latencyMs = 20, .jitterMs = 80, .lossFraction = 0.5F, .duplicateFraction = 0.5F, .reorderFraction = 0.5F});
    std::vector<std::string> sent;
    for (int i = 0; i < 50; ++i) {
        sent.push_back(std::to_string(i));
        REQUIRE(link.client.send(link.peer, Delivery::ReliableOrdered, packet(sent.back())).has_value());
        link.client.advance(1 * kMs);
    }
    link.client.advance(500 * kMs);
    link.client.flush();
    CHECK(link.received() == sent);
}

TEST_CASE("loss drops about its share of unreliable packets", "[net][link_conditioner]") {
    ConditionedLink link;
    link.client.setConditions(LinkConditions{.lossFraction = 0.25F});
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(link.client.send(link.peer, Delivery::Unreliable, packet("x")).has_value());
    }
    link.client.flush();
    const auto arrived = link.received().size();
    CHECK(arrived > 700);
    CHECK(arrived < 800);
    CHECK(link.client.droppedPackets() == 1000 - arrived);
}

TEST_CASE("duplicates reach an unreliable receiver twice but a sequenced one once", "[net][link_conditioner]") {
    ConditionedLink link;
    link.client.setConditions(LinkConditions{.duplicateFraction = 1.0F});
    REQUIRE(link.client.send(link.peer, Delivery::Unreliable, packet("u")).has_value());
    REQUIRE(link.client.send(link.peer, Delivery::UnreliableSequenced, packet("s")).has_value());
    link.client.flush();
    CHECK(link.received() == std::vector<std::string>{"u", "u", "s"});
}

TEST_CASE("reordering mixes unreliable packets but never lets an old sequenced one through",
          "[net][link_conditioner]") {
    ConditionedLink link;
    link.client.setConditions(LinkConditions{.reorderFraction = 0.3F, .reorderDelayMs = 10});
    for (int i = 0; i < 100; ++i) {
        REQUIRE(link.client.send(link.peer, Delivery::Unreliable, packet("u" + std::to_string(1000 + i))).has_value());
        REQUIRE(link.client.send(link.peer, Delivery::UnreliableSequenced, packet("s" + std::to_string(1000 + i)))
                    .has_value());
        link.client.advance(1 * kMs);
    }
    link.client.advance(100 * kMs);
    std::vector<std::string> unreliable;
    std::vector<std::string> sequenced;
    for (const std::string& p : link.received()) {
        (p[0] == 'u' ? unreliable : sequenced).push_back(p.substr(1));
    }
    // Every unreliable packet arrives (no loss is set), some out of order.
    CHECK(unreliable.size() == 100);
    CHECK_FALSE(std::is_sorted(unreliable.begin(), unreliable.end()));
    // The sequenced ones arrive strictly in order, with the overtaken ones dropped.
    CHECK(sequenced.size() < 100);
    CHECK(std::adjacent_find(sequenced.begin(), sequenced.end(), std::greater_equal<>()) == sequenced.end());
}

TEST_CASE("the same seed gives the same conditions on every run", "[net][link_conditioner]") {
    // Two links with the same seed and conditions drop the same packets.
    const auto run = [] {
        ConditionedLink link;
        link.client.setConditions(LinkConditions{.lossFraction = 0.5F});
        for (int i = 0; i < 64; ++i) {
            REQUIRE(link.client.send(link.peer, Delivery::Unreliable, packet(std::to_string(i))).has_value());
        }
        link.client.flush();
        return link.received();
    };
    CHECK(run() == run());
}

TEST_CASE("a close waits for the reliable packets held before it", "[net][link_conditioner]") {
    ConditionedLink link;
    link.client.setConditions(LinkConditions{.latencyMs = 50});
    REQUIRE(link.client.send(link.peer, Delivery::Unreliable, packet("gone")).has_value());
    link.client.advance(10 * kMs);
    REQUIRE(link.client.send(link.peer, Delivery::ReliableOrdered, packet("bye")).has_value());
    link.client.disconnect(link.peer);
    CHECK_FALSE(link.client.send(link.peer, Delivery::ReliableOrdered, packet("after")).has_value());
    // At 50 ms the unreliable packet goes; the close waits for the reliable one at 60 ms.
    link.client.advance(40 * kMs);
    CHECK(link.received() == std::vector<std::string>{"gone"});
    link.client.advance(10 * kMs);
    const auto events = drain(link.server);
    REQUIRE(events.size() == 2);
    CHECK(text(events[0].data) == "bye");
    CHECK(events[1].kind == TransportEventKind::Disconnected);
}
