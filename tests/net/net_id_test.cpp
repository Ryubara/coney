// SPDX-License-Identifier: GPL-3.0-or-later
// Network ids (docs/guides/multiplayer.md#net-ids): the server's allocator gives a slot back only after its removal is
// acknowledged, and then only with a new generation; the ids' wire form; the table from ids to each side's handles
// (pointers such as Human* and Brain*, and script handles), which never answers a stale generation.
#include "net/net_id.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "net/bit_stream.h"
#include "net/net_id_table.h"

using coney::ErrorCode;
using coney::net::BitReader;
using coney::net::BitWriter;
using coney::net::kNetIdBits;
using coney::net::kNetIdMaxGeneration;
using coney::net::kNetIdMaxSlots;
using coney::net::NetId;
using coney::net::NetIdAllocator;
using coney::net::NetIdTable;

namespace {

// Stands in for a game object the server names by pointer (a Human, a Brain): only its address matters here.
struct Thing {
    int value = 0;
};

// Allocates an id that must succeed.
NetId mustAllocate(NetIdAllocator& ids) {
    const auto id = ids.allocate();
    REQUIRE(id.has_value());
    return *id;
}

} // namespace

// Expected: unverified, #553 (Coney's own network ids; the original has no network play)
TEST_CASE("net ids: fresh slots come in order, each with generation 1, and the default id is none", "[net][net_id]") {
    CHECK_FALSE(NetId{}.valid());
    NetIdAllocator ids(4);
    for (std::uint16_t slot = 0; slot < 4; ++slot) {
        const NetId id = mustAllocate(ids);
        CHECK(id == NetId{.slot = slot, .generation = 1});
        CHECK(ids.live(id));
    }
    CHECK(ids.liveCount() == 4);
    const auto full = ids.allocate();
    REQUIRE_FALSE(full.has_value());
    CHECK(full.error().code == ErrorCode::Invalid);
}

// Expected: unverified, #553 (Coney's own network ids; the original has no network play)
TEST_CASE("net ids: a slot is reused only after its release, and only with a new generation", "[net][net_id]") {
    NetIdAllocator ids(2);
    const NetId first = mustAllocate(ids);
    const NetId second = mustAllocate(ids);
    ids.retire(first);
    CHECK_FALSE(ids.live(first));
    CHECK(ids.liveCount() == 1);
    CHECK_FALSE(ids.allocate().has_value()); // retired is not free: a client may still name it

    CHECK(ids.release(first));
    CHECK_FALSE(ids.release(first)); // a repeated acknowledgement changes nothing
    const NetId reused = mustAllocate(ids);
    CHECK(reused.slot == first.slot);
    CHECK(reused.generation == first.generation + 1);
    CHECK(reused != first);
    CHECK(ids.live(reused));
    CHECK_FALSE(ids.live(first)); // the old id never comes back to life
    CHECK(ids.live(second));
}

// Expected: unverified, #553 (Coney's own network ids; the original has no network play)
TEST_CASE("net ids: the slots never used come before released ones, and released ones oldest first", "[net][net_id]") {
    NetIdAllocator ids(4);
    const NetId a = mustAllocate(ids);
    const NetId b = mustAllocate(ids);
    ids.retire(b);
    ids.retire(a);
    CHECK(ids.release(b)); // b released first, so it rests longer and comes back first
    CHECK(ids.release(a));
    CHECK(mustAllocate(ids).slot == 2);
    CHECK(mustAllocate(ids).slot == 3);
    CHECK(mustAllocate(ids) == NetId{.slot = b.slot, .generation = 2});
    CHECK(mustAllocate(ids) == NetId{.slot = a.slot, .generation = 2});
}

// Expected: unverified, #553 (Coney's own network ids; the original has no network play)
TEST_CASE("net ids: release refuses an id that is live or of another generation", "[net][net_id]") {
    NetIdAllocator ids(1);
    const NetId id = mustAllocate(ids);
    CHECK_FALSE(ids.release(id)); // live, not retired
    ids.retire(id);
    CHECK_FALSE(ids.release(NetId{.slot = id.slot, .generation = 2}));
    CHECK_FALSE(ids.release(NetId{.slot = 7, .generation = 1})); // a slot never used
    CHECK(ids.release(id));
}

// Expected: unverified, #553 (Coney's own network ids; the original has no network play)
TEST_CASE("net ids: a slot's generations run 1 to the last, then wrap to 1, never 0", "[net][net_id]") {
    NetIdAllocator ids(1);
    std::set<std::uint8_t> seen;
    NetId id = mustAllocate(ids);
    for (int round = 0; round < kNetIdMaxGeneration; ++round) {
        seen.insert(id.generation);
        ids.retire(id);
        REQUIRE(ids.release(id));
        const NetId next = mustAllocate(ids);
        CHECK(next.generation != id.generation);
        CHECK(next.generation != 0);
        id = next;
    }
    CHECK(seen.size() == kNetIdMaxGeneration); // every generation once before any repeats
    CHECK(id.generation == 1);
}

// Expected: unverified, #553 (Coney's own network ids; the original has no network play)
TEST_CASE("net ids: an allocator may use every slot the wire can name", "[net][net_id]") {
    NetIdAllocator ids(kNetIdMaxSlots);
    NetId last;
    for (std::size_t i = 0; i < kNetIdMaxSlots; ++i) {
        last = mustAllocate(ids);
    }
    CHECK(last.slot == kNetIdMaxSlots - 1);
    CHECK_FALSE(ids.allocate().has_value());
}

// Expected: unverified, #553 (Coney's own network ids; the original has no network play)
TEST_CASE("net ids: an id's wire form round-trips none, the first and the last ids", "[net][net_id]") {
    const std::array<NetId, 4> sent{NetId{}, NetId{.slot = 0, .generation = 1}, NetId{.slot = 300, .generation = 9},
                                    NetId{.slot = 0xffff, .generation = kNetIdMaxGeneration}};
    BitWriter out;
    for (const NetId id : sent) {
        coney::net::writeNetId(out, id);
    }
    CHECK(out.bitCount() == sent.size() * kNetIdBits);
    BitReader in(out.bytes());
    for (const NetId id : sent) {
        const auto read = coney::net::readNetId(in);
        REQUIRE(read.has_value());
        CHECK(*read == id);
    }
    CHECK(in.finish().has_value());
    const auto past = coney::net::readNetId(in);
    REQUIRE_FALSE(past.has_value());
    CHECK(past.error().code == ErrorCode::Truncated);
}

// Expected: unverified, #553 (Coney's own network ids; the original has no network play)
TEST_CASE("net id table: binds an id to a pointer both ways, and a stale generation finds nothing", "[net][net_id]") {
    std::array<Thing, 2> things{};
    NetIdAllocator ids(1);
    NetIdTable<const Thing*> table;
    const auto id = table.assign(ids, &things[0]);
    REQUIRE(id.has_value());
    CHECK(table.assign(ids, &things[0]) == *id); // already replicated: the same id
    CHECK(table.handleOf(*id) == &things[0]);
    CHECK(table.idOf(&things[0]) == *id);
    CHECK_FALSE(table.idOf(&things[1]).has_value());

    // The thing goes; once released, its slot comes back for another with a new generation.
    CHECK(table.retire(ids, &things[0]) == *id);
    CHECK_FALSE(table.retire(ids, &things[0]).has_value());
    CHECK_FALSE(table.handleOf(*id).has_value());
    CHECK(ids.release(*id));
    const auto next = table.assign(ids, &things[1]);
    REQUIRE(next.has_value());
    CHECK(next->slot == id->slot);
    CHECK(table.handleOf(*next) == &things[1]);
    CHECK_FALSE(table.handleOf(*id).has_value()); // a late packet's old id does not find the new thing
    CHECK(table.size() == 1);
}

// Expected: unverified, #553 (Coney's own network ids; the original has no network play)
TEST_CASE("net id table: a client's binds refuse what the network could get wrong", "[net][net_id]") {
    // A client binds script handles (doubles) to the ids the server sends.
    NetIdTable<double> table;
    const NetId id{.slot = 5, .generation = 3};
    REQUIRE(table.bind(id, 12.0).has_value());

    const auto none = table.bind(NetId{}, 13.0);
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().code == ErrorCode::Invalid);
    const auto sameSlot = table.bind(NetId{.slot = 5, .generation = 4}, 13.0); // the old one not unbound yet
    REQUIRE_FALSE(sameSlot.has_value());
    CHECK(sameSlot.error().code == ErrorCode::Invalid);
    const auto sameHandle = table.bind(NetId{.slot = 6, .generation = 1}, 12.0);
    REQUIRE_FALSE(sameHandle.has_value());
    CHECK(sameHandle.error().code == ErrorCode::Invalid);

    CHECK_FALSE(table.unbind(NetId{.slot = 5, .generation = 4})); // not this generation
    CHECK(table.unbind(id));
    CHECK_FALSE(table.idOf(12.0).has_value());
    CHECK(table.bind(NetId{.slot = 5, .generation = 4}, 12.0).has_value());
}

// Expected: unverified, #553 (Coney's own network ids; the original has no network play)
TEST_CASE("net id table: walks in id order, whatever the handles' values", "[net][net_id]") {
    NetIdTable<double> table;
    REQUIRE(table.bind(NetId{.slot = 9, .generation = 1}, 1.0).has_value());
    REQUIRE(table.bind(NetId{.slot = 2, .generation = 7}, 3.0).has_value());
    REQUIRE(table.bind(NetId{.slot = 4, .generation = 2}, 2.0).has_value());
    std::vector<std::uint16_t> slots;
    table.forEach([&](NetId id, double /*handle*/) { slots.push_back(id.slot); });
    CHECK(slots == std::vector<std::uint16_t>{2, 4, 9});
}

// Expected: unverified, #554 (Coney's own network ids; a human with a brain is one thing on the wire)
TEST_CASE("net id table: a human that is also a brain gets one id, whichever table reaches it first", "[net][net_id]") {
    // Stands in for a Human and its Brain: two handles of two kinds for one replicated thing.
    struct Body {
        int value = 0;
    };
    struct Mind {
        int value = 0;
    };
    Body body;
    Mind mind;
    for (const bool bodyFirst : {true, false}) {
        INFO(std::string(bodyFirst ? "the human's table first" : "the brain's table first"));
        NetIdAllocator ids(4);
        NetIdTable<const Body*> bodies;
        NetIdTable<const Mind*> minds;
        const auto first = bodyFirst ? coney::net::assignShared(ids, bodies, &body, minds, &mind)
                                     : coney::net::assignShared(ids, minds, &mind, bodies, &body);
        REQUIRE(first.has_value());
        const auto second = bodyFirst ? coney::net::assignShared(ids, minds, &mind, bodies, &body)
                                      : coney::net::assignShared(ids, bodies, &body, minds, &mind);
        REQUIRE(second.has_value());
        CHECK(*second == *first);
        CHECK(ids.liveCount() == 1);
        CHECK(bodies.idOf(&body) == *first);
        CHECK(minds.idOf(&mind) == *first);

        // Gone: whichever path finds it first retires its one id; the other finds nothing left.
        const auto retired = bodyFirst ? coney::net::retireShared(ids, bodies, &body, minds, &mind)
                                       : coney::net::retireShared(ids, minds, &mind, bodies, &body);
        CHECK(retired == *first);
        const auto again = bodyFirst ? coney::net::retireShared(ids, minds, &mind, bodies, &body)
                                     : coney::net::retireShared(ids, bodies, &body, minds, &mind);
        CHECK_FALSE(again.has_value());
        CHECK(ids.liveCount() == 0);
        CHECK(bodies.size() == 0);
        CHECK(minds.size() == 0);
        CHECK(ids.release(*first));
    }

    // One table already holds an id (the human replicated before its brain was made): the other takes the same one.
    NetIdAllocator ids(4);
    NetIdTable<const Body*> bodies;
    NetIdTable<const Mind*> minds;
    const auto alone = bodies.assign(ids, &body);
    REQUIRE(alone.has_value());
    CHECK(coney::net::assignShared(ids, minds, &mind, bodies, &body) == *alone);
    CHECK(ids.liveCount() == 1);
    CHECK(minds.idOf(&mind) == *alone);
}
