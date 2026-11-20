// SPDX-License-Identifier: GPL-3.0-or-later
// The game events' way from the server to a client (docs/guides/multiplayer.md#events): EventBatch's encoding, pinned
// and refused when broken; the sender's batches; the receiver's repeats, late one-shots and order; and, over the
// loopback, every kind of event raised on the server handed over once on the client at its step, with a clean link and
// with loss, repeats and reordering.
#include "net/event_channel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_events.h"
#include "net/link_conditioner.h"
#include "net/loopback_transport.h"
#include "net/net_client.h"
#include "net/net_id.h"
#include "net/net_server.h"
#include "net/protocol.h"

using namespace coney::net;
using coney::GameEvent;
using coney::GameEventKind;

namespace {

constexpr std::uint64_t kStepNs = 33'333'333; // one 30 Hz step

// The bytes `values` name, as a packet.
std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

// One event of each kind, with every field its kind carries set to something other than its default.
std::vector<GameEvent> everyKind() {
    using enum GameEventKind;
    return {
        GameEvent{.kind = Sound2D, .id = 0x11111111},
        GameEvent{.kind = Sound3D, .id = 0x22, .position = {1.5F, -2.0F, 0.25F}},
        GameEvent{.kind = HumanSound, .subject = 7, .id = 0x33, .value = 1, .position = {3.0F, 4.0F, 0.0F}},
        GameEvent{.kind = Hit, .subject = 8, .id = 12, .value = 14, .position = {-1.0F, 0.5F, 1.0F}},
        GameEvent{.kind = MusicStart, .id = 0x44, .value = 3, .extra = 2},
        GameEvent{.kind = MusicStop},
        GameEvent{.kind = LoopStart, .id = 0x55},
        GameEvent{.kind = LoopStop},
        GameEvent{.kind = ParticleStart,
                  .subject = 9,
                  .id = 0xff00ff80,
                  .value = 42,
                  .position = {0.0F, 1.0F, 2.0F},
                  .text = "spark",
                  .rotation = {0.0F, 0.0F, 0.5F, 0.866F}},
        GameEvent{.kind = ObjectBreak, .subject = 10, .id = 2, .position = {5.0F, 5.0F, 0.0F}},
        GameEvent{.kind = HudMessage, .id = 5, .value = 1, .text = "<DISPLAYTIME 4000>Go"},
        GameEvent{.kind = Objective, .id = 1, .value = 3, .extra = 8000, .text = "Find the Warriors"},
        GameEvent{.kind = SceneStart, .id = 42},
        GameEvent{.kind = SceneEnd, .id = 42},
        GameEvent{.kind = MovieStart, .value = 1, .text = "INTRO"},
        GameEvent{.kind = MovieEnd, .value = 1, .text = "INTRO"},
        GameEvent{.kind = Speech, .subject = 11, .value = 1, .text = "vags/speeches/l99/hi"},
        GameEvent{.kind = Command, .subject = 12, .id = 75, .value = 3, .extra = 2},
        GameEvent{.kind = SpeechStop, .subject = 11, .value = 1},
        GameEvent{.kind = MaterialPair, .id = 2, .value = 5, .extra = 250, .position = {1.0F, 1.0F, 0.5F}},
        GameEvent{.kind = Cue, .subject = 13, .id = 23, .value = 1, .position = {2.0F, 0.0F, 1.0F}},
        GameEvent{.kind = Hint, .id = 0, .value = 3, .text = "Press <X>"},
    };
}

// The ids the tests' server gives subjects: handle h is slot h, generation 1.
NetId idOf(double handle) { return NetId{.slot = static_cast<std::uint16_t>(handle), .generation = 1}; }

// A server and one client on a loopback network, each behind a link conditioner, stepped as the main loop steps them.
struct Pair {
    LoopbackNetwork network;
    LoopbackTransport serverWire{network, "host"};
    LinkConditioner serverLink{serverWire, 7};
    NetServer server{serverLink, config()};
    std::unique_ptr<LoopbackTransport> clientWire;
    std::unique_ptr<LinkConditioner> clientLink;
    std::unique_ptr<NetClient> client;
    std::uint32_t step = 0; // the server's next step

    Pair() { REQUIRE(server.start().has_value()); }

    // The tests' server: any free port.
    static ServerConfig config() {
        ServerConfig config;
        config.port = 0;
        config.maxClients = 2;
        return config;
    }

    // Starts the client connecting and runs until it has joined.
    void join() {
        clientWire = std::make_unique<LoopbackTransport>(network, "guest");
        clientLink = std::make_unique<LinkConditioner>(*clientWire, 99);
        client = std::make_unique<NetClient>(*clientLink, ClientConfig{});
        REQUIRE(client->connect("host", server.port()).has_value());
        for (int i = 0; i < 10 && client->state() != ClientState::Connected; ++i) {
            run(1);
        }
        REQUIRE(client->state() == ClientState::Connected);
    }

    // Runs `steps` steps; in each, the server sends the events `raise` gives for that step (by the step's number).
    void run(int steps, const std::map<std::uint32_t, std::vector<GameEvent>>& raise = {}) {
        for (int i = 0; i < steps; ++i) {
            server.advance(kStepNs);
            if (client) {
                client->advance(kStepNs);
            }
            server.receive();
            if (client) {
                client->receive();
            }
            if (const auto found = raise.find(step); found != raise.end()) {
                server.sendEvents(step, toNetEvents(found->second, idOf));
            }
            ++step;
            server.send();
            if (client) {
                client->send();
            }
        }
    }
};

} // namespace

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("event batches: one sound's bytes are pinned", "[net][events]") {
    // Id 12; reliable 0; sequence 1 and step 2 as varints; count less one 0 (8 bits); kind 1 (8 bits); ordinal 0; the
    // sound's hash 5 as a varint.
    const EventBatch batch{.reliable = false,
                           .sequence = 1,
                           .step = 2,
                           .events = {NetEvent{.kind = GameEventKind::Sound2D, .ordinal = 0, .id = 5}}};
    CHECK(encode(batch) == bytes({0x0c, 0x02, 0x04, 0x00, 0x02, 0x00, 0x0a, 0x00}));
    CHECK(deliveryOf(batch) == Delivery::Unreliable);
    EventBatch reliable = batch;
    reliable.reliable = true;
    reliable.events[0] = NetEvent{.kind = GameEventKind::SceneStart, .ordinal = 0, .id = 5};
    CHECK(deliveryOf(reliable) == Delivery::ReliableOrdered);
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("event batches: every kind round-trips with the fields it carries", "[net][events]") {
    const std::vector<NetEvent> events = toNetEvents(everyKind(), idOf);
    REQUIRE(events.size() == 22);
    for (const bool reliable : {true, false}) {
        EventBatch batch{.reliable = reliable, .sequence = 9, .step = 300, .events = {}};
        for (const NetEvent& event : events) {
            if (coney::reliableGameEvent(event.kind) == reliable) {
                batch.events.push_back(event);
            }
        }
        const auto decoded = decode(encode(batch));
        REQUIRE(decoded.has_value());
        CHECK(std::get<EventBatch>(*decoded) == batch);
    }
    // The subject travels as the id the server's table gives it; a kind without one sends none.
    CHECK(events[2].subject == idOf(7));
    CHECK_FALSE(events[0].subject.valid());
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("event batches: a broken one is refused", "[net][events]") {
    const std::vector<std::byte> good = bytes({0x0c, 0x02, 0x04, 0x00, 0x02, 0x00, 0x0a, 0x00});
    REQUIRE(decode(good).has_value());
    // Sequence 0.
    CHECK_FALSE(decode(bytes({0x0c, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0a, 0x00})).has_value());
    // Kind 23, unknown.
    CHECK_FALSE(decode(bytes({0x0c, 0x02, 0x04, 0x00, 0x2e, 0x00, 0x0a, 0x00})).has_value());
    // Reliable set with a one-shot in it.
    CHECK_FALSE(decode(bytes({0x0c, 0x03, 0x04, 0x00, 0x02, 0x00, 0x0a, 0x00})).has_value());
    // Short, and with a trailing byte.
    CHECK_FALSE(decode(bytes({0x0c, 0x02, 0x04, 0x00, 0x02, 0x00})).has_value());
    CHECK_FALSE(decode(bytes({0x0c, 0x02, 0x04, 0x00, 0x02, 0x00, 0x0a, 0x00, 0x00})).has_value());
    // Two events whose ordinals do not rise.
    const EventBatch twice{.reliable = false,
                           .sequence = 1,
                           .step = 0,
                           .events = {NetEvent{.kind = GameEventKind::Sound2D, .ordinal = 1, .id = 1},
                                      NetEvent{.kind = GameEventKind::Sound2D, .ordinal = 2, .id = 2}}};
    std::vector<std::byte> packet = encode(twice);
    REQUIRE(decode(packet).has_value());
    // The second event's ordinal is the varint at bits 57-64 after the id byte (header 25 bits, the first event 24,
    // the second's kind 8): 2 there becomes 1, the first event's, so they no longer rise.
    REQUIRE(packet.size() > 8);
    packet[8] ^= std::byte{0x06};
    CHECK_FALSE(decode(packet).has_value());
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("event sender: batches by delivery, numbered apart, split at the budget", "[net][events]") {
    EventSender sender;
    const std::vector<NetEvent> events = toNetEvents(everyKind(), {});
    const std::vector<EventBatch> first = sender.build(4, events);
    REQUIRE(first.size() == 2);
    CHECK(first[0].reliable);
    CHECK(first[0].sequence == 1);
    CHECK(first[0].events.size() == 14);
    CHECK_FALSE(first[1].reliable);
    CHECK(first[1].sequence == 1);
    CHECK(first[1].events.size() == 8);
    CHECK(first[1].step == 4);
    // The next step's one-shots take the next one-shot number; no reliable batch is made for none.
    const std::vector<EventBatch> second = sender.build(5, std::span(events).first(1));
    REQUIRE(second.size() == 1);
    CHECK(second[0].sequence == 2);
    CHECK(sender.lastSequence(true) == 1);
    CHECK(sender.build(6, {}).empty());
    // Many long messages fill several batches, each within the budget, ordinals kept rising across them.
    std::vector<GameEvent> messages(40, GameEvent{.kind = GameEventKind::HudMessage, .text = std::string(200, 'm')});
    const std::vector<EventBatch> split = sender.build(7, toNetEvents(messages, {}));
    REQUIRE(split.size() > 1);
    std::size_t total = 0;
    for (const EventBatch& batch : split) {
        CHECK(encode(batch).size() <= kEventBatchBudgetBytes);
        total += batch.events.size();
    }
    CHECK(total == 40);
    CHECK(split.back().events.back().ordinal == 39);
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("event receiver: repeats dropped, steps handed over in order, late one-shots dropped", "[net][events]") {
    EventReceiver receiver;
    const auto sound = [](std::uint16_t ordinal, std::uint32_t id) {
        return NetEvent{.kind = GameEventKind::Sound2D, .ordinal = ordinal, .id = id};
    };
    const auto scene = [](std::uint16_t ordinal, std::uint32_t id) {
        return NetEvent{.kind = GameEventKind::SceneStart, .ordinal = ordinal, .id = id};
    };
    // Step 3's one-shots arrive before step 2's, and step 2's reliable batch carries ordinal 0 before the sound's 1.
    REQUIRE(receiver.apply({.reliable = false, .sequence = 2, .step = 3, .events = {sound(0, 30)}}) ==
            EventsApplied::Applied);
    REQUIRE(receiver.apply({.reliable = false, .sequence = 1, .step = 2, .events = {sound(1, 21)}}) ==
            EventsApplied::Applied);
    REQUIRE(receiver.apply({.reliable = true, .sequence = 1, .step = 2, .events = {scene(0, 20)}}) ==
            EventsApplied::Applied);
    // A repeat of either delivery changes nothing.
    CHECK(receiver.apply({.reliable = false, .sequence = 1, .step = 2, .events = {sound(1, 21)}}) ==
          EventsApplied::Duplicate);
    CHECK(receiver.apply({.reliable = true, .sequence = 1, .step = 2, .events = {scene(0, 20)}}) ==
          EventsApplied::Duplicate);
    CHECK(receiver.pending() == 3);
    // Nothing before its step.
    CHECK(receiver.take(1).empty());
    const std::vector<ArrivedEvent> two = receiver.take(2);
    REQUIRE(two.size() == 2);
    CHECK(two[0] == ArrivedEvent{.step = 2, .event = scene(0, 20)});
    CHECK(two[1] == ArrivedEvent{.step = 2, .event = sound(1, 21)});
    // A one-shot for a step already handed over is dropped; a reliable one still comes, at the next take.
    REQUIRE(receiver.apply({.reliable = false, .sequence = 3, .step = 2, .events = {sound(2, 22)}}) ==
            EventsApplied::Applied);
    REQUIRE(receiver.apply({.reliable = true, .sequence = 2, .step = 1, .events = {scene(0, 10)}}) ==
            EventsApplied::Applied);
    const std::vector<ArrivedEvent> three = receiver.take(3);
    REQUIRE(three.size() == 2);
    CHECK(three[0] == ArrivedEvent{.step = 1, .event = scene(0, 10)});
    CHECK(three[1] == ArrivedEvent{.step = 3, .event = sound(0, 30)});
    CHECK(receiver.stats().late == 1);
    CHECK(receiver.stats().duplicates == 2);
    CHECK(receiver.stats().handed == 4);
    // A one-shots' batch older than the window is stale.
    REQUIRE(receiver.apply({.reliable = false, .sequence = 100, .step = 9, .events = {sound(0, 90)}}) ==
            EventsApplied::Applied);
    CHECK(receiver.apply({.reliable = false, .sequence = 4, .step = 9, .events = {sound(1, 91)}}) ==
          EventsApplied::Stale);
    // The same step and ordinal under another sequence is a broken server.
    CHECK_FALSE(receiver.apply({.reliable = false, .sequence = 101, .step = 9, .events = {sound(0, 92)}}).has_value());
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("event channel: every kind raised on the server arrives once on the client at its step", "[net][events]") {
    Pair pair;
    pair.join();
    const std::uint32_t raisedAt = pair.step;
    pair.run(1, {{raisedAt, everyKind()}});
    pair.run(2);
    // Nothing is handed over before the step it happened in.
    CHECK(pair.client->takeEvents(raisedAt - 1).empty());
    const std::vector<ArrivedEvent> arrived = pair.client->takeEvents(raisedAt);
    const std::vector<NetEvent> sent = toNetEvents(everyKind(), idOf);
    REQUIRE(sent.size() == 22);
    REQUIRE(arrived.size() == sent.size());
    for (std::size_t i = 0; i < sent.size(); ++i) {
        CHECK(arrived[i].step == raisedAt);
        CHECK(arrived[i].event == sent[i]);
    }
    // Once each.
    pair.run(3);
    CHECK(pair.client->takeEvents(pair.step).empty());
    CHECK(pair.client->state() == ClientState::Connected);
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("event channel: over loss, repeats and reordering, reliable events arrive once in order, one-shots at most "
          "once",
          "[net][events]") {
    Pair pair;
    pair.join();
    pair.serverLink.setConditions(LinkConditions{
        .latencyMs = 30, .jitterMs = 20, .lossFraction = 0.3F, .duplicateFraction = 0.3F, .reorderFraction = 0.3F});
    // Forty steps, each raising every kind.
    std::map<std::uint32_t, std::vector<GameEvent>> raise;
    const std::uint32_t first = pair.step;
    for (std::uint32_t s = first; s < first + 40; ++s) {
        raise[s] = everyKind();
    }
    pair.run(40, raise);
    pair.serverLink.setConditions(LinkConditions{});
    pair.run(30);
    const std::vector<ArrivedEvent> arrived = pair.client->takeEvents(pair.step);
    std::map<std::pair<std::uint32_t, std::uint16_t>, int> seen;
    std::size_t reliable = 0;
    std::size_t oneShots = 0;
    for (std::size_t i = 0; i < arrived.size(); ++i) {
        const ArrivedEvent& event = arrived[i];
        ++seen[{event.step, event.event.ordinal}];
        (coney::reliableGameEvent(event.event.kind) ? reliable : oneShots) += 1;
        if (i > 0) {
            const ArrivedEvent& before = arrived[i - 1];
            CHECK((before.step < event.step ||
                   (before.step == event.step && before.event.ordinal < event.event.ordinal)));
        }
    }
    for (const auto& [key, count] : seen) {
        CHECK(count == 1);
    }
    CHECK(reliable == std::size_t{40} * 14);
    CHECK(oneShots <= std::size_t{40} * 8);
    CHECK(oneShots > 0);
    CHECK(pair.client->state() == ClientState::Connected);
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("event channel: a player who joins later gets none of the events before", "[net][events]") {
    Pair pair;
    pair.run(1, {{0, everyKind()}}); // nobody joined: nothing to send to
    pair.join();
    const std::uint32_t at = pair.step;
    pair.run(2, {{at, {GameEvent{.kind = GameEventKind::MusicStop}}}});
    const std::vector<ArrivedEvent> arrived = pair.client->takeEvents(pair.step);
    REQUIRE(arrived.size() == 1);
    CHECK(arrived[0].step == at);
    CHECK(arrived[0].event.kind == GameEventKind::MusicStop);
}
