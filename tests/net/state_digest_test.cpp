// SPDX-License-Identifier: GPL-3.0-or-later
// The state digest (docs/guides/multiplayer.md#digest): a hash of the replicated humans and of the pads a step played,
// the same on every machine; the first human and field two holdings differ in; the digest lines a run writes, read
// back strictly; and the comparison of a guest's lines with the host's.
#include "net/state_digest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/pad.h"
#include "core/pads.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"
#include "net/snapshot.h"
#include "support/optional_value.h"

using coney::ErrorCode;
using coney::PadSample;
using coney::PortSamples;
using coney::test::got;
using namespace coney::net;

namespace {

// Field `f`'s index into a human's words.
constexpr std::size_t at(HumanField f) { return static_cast<std::size_t>(f); }

// The id of the `slot`th human, generation 1.
NetId idOf(std::uint16_t slot) { return NetId{.slot = slot, .generation = 1}; }

// A human whose words are all `base` plus their index, so every field differs from every other.
HumanWords wordsFrom(std::uint32_t base) {
    HumanWords words{};
    for (std::size_t i = 0; i < words.size(); ++i) {
        words.at(i) = base + static_cast<std::uint32_t>(i);
    }
    return words;
}

// Two humans, ids 1 and 2.
SnapshotHumans twoHumans() { return {{idOf(1), wordsFrom(10)}, {idOf(2), wordsFrom(100)}}; }

} // namespace

TEST_CASE("the humans' digest is pinned and the same however the map was filled") {
    // Expected: unverified, #558 (Coney's own hash; the original has no network play)
    // FNV-1a 64 over nothing but the count 0: a pinned value, so a change to the hash cannot slip in unnoticed.
    CHECK(humansDigest({}) == 0x4d25767f9dce13f5ULL);

    SnapshotHumans backwards;
    backwards.emplace(idOf(2), wordsFrom(100));
    backwards.emplace(idOf(1), wordsFrom(10));
    CHECK(humansDigest(backwards) == humansDigest(twoHumans()));
    CHECK(humansDigest(twoHumans()) != humansDigest({}));
}

TEST_CASE("any change to a human moves the digest") {
    // Expected: unverified, #558 (Coney's own hash)
    const std::uint64_t before = humansDigest(twoHumans());

    SnapshotHumans moved = twoHumans();
    moved.at(idOf(2)).at(at(HumanField::PositionX)) += 1;
    CHECK(humansDigest(moved) != before);

    SnapshotHumans regenerated = twoHumans();
    const HumanWords words = regenerated.at(idOf(2));
    regenerated.erase(idOf(2));
    regenerated.emplace(NetId{.slot = 2, .generation = 2}, words);
    CHECK(humansDigest(regenerated) != before);

    SnapshotHumans fewer = twoHumans();
    fewer.erase(idOf(1));
    CHECK(humansDigest(fewer) != before);

    // Two humans' words swapped between their ids are another state.
    SnapshotHumans swapped{{idOf(1), wordsFrom(100)}, {idOf(2), wordsFrom(10)}};
    CHECK(humansDigest(swapped) != before);
}

TEST_CASE("the pads' digest sees what a step played and nothing a disconnected pad carries") {
    // Expected: unverified, #558 (Coney's own hash)
    const PortSamples rest{};
    PortSamples pressed{};
    pressed[1].connected = true;
    pressed[1].buttons = coney::pad::kCross;
    CHECK(padsDigest(pressed) != padsDigest(rest));

    PortSamples pressure = pressed;
    pressure[1].pressure.at(0) = 200;
    CHECK(padsDigest(pressure) != padsDigest(pressed));

    PortSamples stick = pressed;
    stick[1].sticks.at(2) = 0;
    CHECK(padsDigest(stick) != padsDigest(pressed));

    // Port 1 and port 2 are not interchangeable.
    PortSamples otherPort{};
    otherPort[0] = pressed[1];
    CHECK(padsDigest(otherPort) != padsDigest(pressed));

    // An unplugged pad's other bytes are ignored by the game, so by the digest too.
    PortSamples unpluggedJunk{};
    unpluggedJunk[0].buttons = 0xffff;
    unpluggedJunk[0].sticks.at(0) = 3;
    CHECK(padsDigest(unpluggedJunk) == padsDigest(rest));
}

TEST_CASE("the first difference names the human and the field") {
    // Expected: unverified, #558 (Coney's own debug dump)
    CHECK_FALSE(firstDifference(twoHumans(), twoHumans()).has_value());

    SnapshotHumans changed = twoHumans();
    changed.at(idOf(2)).at(at(HumanField::Heading)) += 3;
    changed.at(idOf(2)).at(at(HumanField::Grounded)) ^= 1U;
    const auto field = firstDifference(twoHumans(), changed);
    REQUIRE(field.has_value());
    CHECK(got(field).id == idOf(2));
    CHECK(got(field).field == HumanField::Heading);
    CHECK(describeDifference(got(field), twoHumans(), changed) == "human 2.1 heading: 103 against 106");

    SnapshotHumans missing = twoHumans();
    missing.erase(idOf(1));
    const auto gone = firstDifference(twoHumans(), missing);
    REQUIRE(gone.has_value());
    CHECK(got(gone).id == idOf(1));
    CHECK_FALSE(got(gone).field.has_value());
    CHECK(describeDifference(got(gone), twoHumans(), missing) == "human 1.1: held by the first only");
    const auto extra = firstDifference(missing, twoHumans());
    REQUIRE(extra.has_value());
    CHECK(got(extra).id == idOf(1));
    CHECK(describeDifference(got(extra), missing, twoHumans()) == "human 1.1: held by the second only");
}

TEST_CASE("a digest line round-trips and a malformed one is refused") {
    // Expected: unverified, #558 (Coney's own file format, docs/guides/multiplayer.md#digest)
    const DigestLine game{.step = 42, .humans = 0x0123456789abcdefULL, .humanCount = 3, .pads = 0xfedcba9876543210ULL};
    CHECK(formatDigestLine(game) == "digest 42 0123456789abcdef 3 fedcba9876543210");
    const auto read = parseDigestLine(formatDigestLine(game));
    REQUIRE(read.has_value());
    CHECK(*read == game);

    const DigestLine seen{.step = 7, .humans = 0x4d25767f9dce13f5ULL, .humanCount = 0, .pads = std::nullopt};
    CHECK(formatDigestLine(seen) == "digest 7 4d25767f9dce13f5 0");
    const auto readSeen = parseDigestLine(formatDigestLine(seen));
    REQUIRE(readSeen.has_value());
    CHECK(*readSeen == seen);

    for (const char* bad : {"", "digest", "digest 1", "digest x 0123456789abcdef 0", "digest 1 0123 0",
                            "digest 1 0123456789abcdeg 0", "digest 1 0123456789abcdef", "digest 1 0123456789abcdef 0 1",
                            "digest 1 0123456789abcdef 0 fedcba9876543210 9", "dig 1 0123456789abcdef 0",
                            "digest -1 0123456789abcdef 0", "digest 1 0123456789ABCDEF 0"}) {
        INFO(bad);
        const auto refused = parseDigestLine(bad);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::Invalid);
    }
}

TEST_CASE("a guest's digests match the host's where they show the same step") {
    // Expected: unverified, #558 (Coney's own comparison, docs/guides/multiplayer.md#digest)
    const std::vector<DigestLine> host{
        {.step = 1, .humans = 11, .humanCount = 1, .pads = std::nullopt},
        {.step = 2, .humans = 22, .humanCount = 1, .pads = std::nullopt},
        {.step = 3, .humans = 33, .humanCount = 1, .pads = std::nullopt},
        {.step = 4, .humans = 44, .humanCount = 2, .pads = std::nullopt},
    };
    // The guest decoded steps 2 and 4 only: a lost or stale snapshot is fine.
    const std::vector<DigestLine> guest{
        {.step = 2, .humans = 22, .humanCount = 1, .pads = std::nullopt},
        {.step = 4, .humans = 44, .humanCount = 2, .pads = std::nullopt},
    };
    const DigestMatch match = matchDigests(host, guest);
    CHECK(match.matched == 2);
    CHECK_FALSE(match.firstMismatch.has_value());
    CHECK_FALSE(match.firstUnknown.has_value());

    std::vector<DigestLine> wrong = guest;
    wrong[1].humans = 45;
    const DigestMatch bad = matchDigests(host, wrong);
    CHECK(bad.matched == 1);
    REQUIRE(bad.firstMismatch.has_value());
    CHECK(got(bad.firstMismatch) == 4);

    // A step the host never wrote is reported, never silently skipped.
    std::vector<DigestLine> ahead = guest;
    ahead.push_back({.step = 9, .humans = 99, .humanCount = 0, .pads = std::nullopt});
    const DigestMatch unknown = matchDigests(host, ahead);
    REQUIRE(unknown.firstUnknown.has_value());
    CHECK(got(unknown.firstUnknown) == 9);
}
