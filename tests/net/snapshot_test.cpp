// SPDX-License-Identifier: GPL-3.0-or-later
// Snapshots of humans (docs/guides/snapshots.md): deltas against the newest acknowledged snapshot, full ones when none
// is kept, removals, the 1,200-byte budget and the priority accumulator that fills it, the bytes of a busy scene, the
// packet's encoding and refusals, and the round trip through loss, reordering and late acknowledgements.
#include "net/snapshot.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "net/bit_stream.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"

using coney::ErrorCode;
using coney::net::BitWriter;
using namespace coney::net;

namespace {

// Field `f`'s index into a human's words.
constexpr std::size_t at(HumanField f) { return static_cast<std::size_t>(f); }

// The index of the human field named `name` into a human's words; kHumanFieldCount with none.
std::size_t fieldNamed(std::string_view name) {
    const auto fields = fieldTable(ReplicatedKind::Human);
    const auto found = std::ranges::find(fields, name, &FieldSpec::name);
    return static_cast<std::size_t>(std::distance(fields.begin(), found));
}

// The id of the `slot`th human, generation 1.
NetId idOf(std::size_t slot) { return NetId{.slot = static_cast<std::uint16_t>(slot), .generation = 1}; }

// Every human field at its encoding's zero (false, 0 or 0.0), so a field a test leaves alone still has a value of the
// right kind: a layer's gait value, say, is 0 for a layer that is not a walk or run blend.
std::array<FieldValue, kHumanFieldCount> zeroValues() {
    std::array<FieldValue, kHumanFieldCount> values{};
    const auto fields = fieldTable(ReplicatedKind::Human);
    for (std::size_t i = 0; i < kHumanFieldCount; ++i) {
        switch (fields[i].encoding) {
        case FieldEncoding::Bool:
            values.at(i) = false;
            break;
        case FieldEncoding::Integer:
            values.at(i) = std::int32_t{0};
            break;
        case FieldEncoding::Float:
        case FieldEncoding::Angle:
            values.at(i) = 0.0F;
            break;
        }
    }
    return values;
}

// A synthetic human standing in a row: number `index`, at x = 2 m a place, after `moved` steps of walking (each step
// 0.1 m along x, a twentieth of a radian of turn and a thirtieth of the clip on).
SnapshotHuman humanAt(std::size_t index, int moved = 0, std::uint32_t priority = 1) {
    const auto steps = static_cast<float>(moved);
    std::array<FieldValue, kHumanFieldCount> values = zeroValues();
    values.at(at(HumanField::PositionX)) = (2.0F * static_cast<float>(index)) + (0.1F * steps);
    values.at(at(HumanField::PositionY)) = 10.0F;
    values.at(at(HumanField::PositionZ)) = 0.0F;
    values.at(at(HumanField::Heading)) = 0.05F * steps;
    values.at(at(HumanField::Health)) = std::int32_t{100};
    values.at(at(HumanField::HealthMax)) = std::int32_t{100};
    values.at(at(HumanField::Clip)) = std::int32_t{5};
    values.at(at(HumanField::Playhead)) = static_cast<float>(moved % 30) / 30.0F;
    values.at(at(HumanField::Clip1)) = std::int32_t{0}; // no layer 1
    values.at(at(HumanField::Playhead1)) = 0.0F;
    values.at(at(HumanField::Fade1)) = 1.0F;
    values.at(at(HumanField::Clip2)) = std::int32_t{0}; // no layer 2
    values.at(at(HumanField::Playhead2)) = 0.0F;
    values.at(at(HumanField::Fade2)) = 1.0F;
    values.at(at(HumanField::Clip3)) = std::int32_t{0}; // no layer 3
    values.at(at(HumanField::Playhead3)) = 0.0F;
    values.at(at(HumanField::Fade3)) = 1.0F;
    values.at(at(HumanField::Grounded)) = true;
    values.at(at(HumanField::KnockedOut)) = false;
    return SnapshotHuman{.id = idOf(index), .words = humanWords(values), .priority = priority};
}

// A scene of `count` humans in a row, where every `movingEvery`th (0 for none) has walked `step` steps.
std::vector<SnapshotHuman> scene(std::size_t count, int step, std::size_t movingEvery = 0) {
    std::vector<SnapshotHuman> humans;
    for (std::size_t i = 0; i < count; ++i) {
        const bool moving = movingEvery != 0 && i % movingEvery == 0;
        humans.push_back(humanAt(i, moving ? step : 0));
    }
    return humans;
}

// What a list of humans should leave a client holding.
SnapshotHumans holdingOf(const std::vector<SnapshotHuman>& humans) {
    SnapshotHumans holding;
    for (const SnapshotHuman& human : humans) {
        holding.emplace(human.id, human.words);
    }
    return holding;
}

// The snapshot as the client receives it: encoded, then decoded, which must succeed.
Snapshot overTheWire(const Snapshot& snapshot) {
    const auto decoded = decode(encode(snapshot));
    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<Snapshot>(*decoded));
    return std::get<Snapshot>(*decoded);
}

// Sends one snapshot of `humans` from `sender` to `receiver`, which acknowledges it at once.
Snapshot exchange(SnapshotSender& sender, SnapshotReceiver& receiver, std::uint32_t step,
                  const std::vector<SnapshotHuman>& humans) {
    const Snapshot snapshot = sender.build(step, humans);
    const auto applied = receiver.apply(overTheWire(snapshot));
    REQUIRE(applied.has_value());
    REQUIRE(*applied == SnapshotApplied::Applied);
    REQUIRE(sender.acknowledge(snapshot.sequence).value());
    return snapshot;
}

// The test's own xorshift, so the shuffles and losses below are the same on every run and never touch GameRandom.
struct TestRandom {
    std::uint32_t state = 0x9e3779b9U;
    std::uint32_t next() {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        return state;
    }
    bool chance(std::uint32_t percent) { return next() % 100 < percent; }
};

} // namespace

// Expected: unverified, #554 (Coney's own snapshots; the original has no network play)
TEST_CASE("snapshots: the first is full, and a still scene's next ones are empty deltas", "[net][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    const auto humans = scene(3, 0);
    const Snapshot first = exchange(sender, receiver, 10, humans);
    CHECK(first.sequence == 1);
    CHECK(first.baseline == 0);
    CHECK(first.step == 10);
    REQUIRE(first.humans.size() == 3);
    for (const SnapshotEntry& entry : first.humans) {
        CHECK_FALSE(entry.removed);
        CHECK(entry.changed == (1U << kHumanFieldCount) - 1U); // a human new to the client comes in full
    }
    CHECK(receiver.humans() == holdingOf(humans));
    CHECK(receiver.newest() == 1);
    CHECK(receiver.newestStep() == 10);

    const Snapshot second = exchange(sender, receiver, 11, humans);
    CHECK(second.sequence == 2);
    CHECK(second.baseline == 1);
    CHECK(second.humans.empty());
    CHECK(receiver.humans() == holdingOf(humans));
    // Header only: the id, the sequence, one back, the step and no entries, a byte each.
    CHECK(encode(second).size() == 5);
}

// Expected: unverified, #554 (Coney's own snapshots; the original has no network play)
TEST_CASE("snapshots: a delta carries only the fields that changed", "[net][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    auto humans = scene(2, 0);
    (void)exchange(sender, receiver, 0, humans);

    // Human 1 is hurt and steps along x; human 0 does nothing.
    std::array<FieldValue, kHumanFieldCount> values = humanValues(humans[1].words);
    values.at(at(HumanField::PositionX)) = std::get<float>(values.at(at(HumanField::PositionX))) + 0.5F;
    values.at(at(HumanField::Health)) = std::int32_t{-20}; // a lethal hit takes health below 0 (s16)
    humans[1].words = humanWords(values);
    const Snapshot delta = exchange(sender, receiver, 1, humans);
    REQUIRE(delta.humans.size() == 1);
    CHECK(delta.humans[0].id == idOf(1));
    CHECK(delta.humans[0].changed == ((1U << at(HumanField::PositionX)) | (1U << at(HumanField::Health))));
    CHECK(receiver.humans() == holdingOf(humans));
    CHECK(std::get<std::int32_t>(humanValues(receiver.humans().at(idOf(1))).at(at(HumanField::Health))) == -20);

    // A change too small to reach the next quantized step costs nothing.
    values.at(at(HumanField::PositionX)) = std::get<float>(values.at(at(HumanField::PositionX))) + 0.001F;
    humans[1].words = humanWords(values);
    CHECK(exchange(sender, receiver, 2, humans).humans.empty());
}

// Expected: unverified, #554 (Coney's own snapshots; the original has no network play)
TEST_CASE("snapshots: a removed human is removed, and its slot's next generation comes in full", "[net][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    auto humans = scene(3, 0);
    (void)exchange(sender, receiver, 0, humans);

    humans.erase(humans.begin() + 1);
    const Snapshot removal = exchange(sender, receiver, 1, humans);
    REQUIRE(removal.humans.size() == 1);
    CHECK(removal.humans[0].id == idOf(1));
    CHECK(removal.humans[0].removed);
    CHECK(receiver.humans() == holdingOf(humans));

    // Slot 1 comes back for another human, generation 2, while the old one is still held by the baseline in the same
    // snapshot that removes it: both entries, the removal and the new human in full.
    humans = scene(3, 0);
    SnapshotSender again;
    SnapshotReceiver client;
    (void)exchange(again, client, 0, humans);
    humans[1].id.generation = 2;
    const Snapshot swap = exchange(again, client, 1, humans);
    REQUIRE(swap.humans.size() == 2);
    CHECK(swap.humans[0].id == idOf(1));
    CHECK(swap.humans[0].removed);
    CHECK(swap.humans[1].id == NetId{.slot = 1, .generation = 2});
    CHECK(swap.humans[1].changed == (1U << kHumanFieldCount) - 1U);
    CHECK(client.humans() == holdingOf(humans));
}

// Expected: unverified, #554 (Coney's own snapshots; the original has no network play)
TEST_CASE("snapshots: a delta goes against the newest acknowledged, and is full once that leaves the history",
          "[net][snapshot]") {
    SnapshotSender sender;
    const auto humans = scene(2, 0);
    const Snapshot first = sender.build(0, humans);
    CHECK(first.baseline == 0);
    // Not acknowledged yet: the next is full again.
    CHECK(sender.build(1, humans).baseline == 0);
    REQUIRE(sender.acknowledge(1).value());
    CHECK(sender.acknowledged() == 1);
    CHECK(sender.build(2, humans).baseline == 1);
    // A late acknowledgement of an older one changes nothing; a newer one moves the baseline on.
    REQUIRE(sender.acknowledge(3).value());
    CHECK_FALSE(sender.acknowledge(2).value());
    CHECK_FALSE(sender.acknowledge(3).value()); // repeated
    CHECK(sender.acknowledged() == 3);
    // One never sent is refused: a client naming it is broken or lying.
    CHECK(sender.acknowledge(4).error().code == ErrorCode::Invalid);
    CHECK(sender.acknowledge(0).error().code == ErrorCode::Invalid);

    // kSnapshotHistory snapshots on, 3 is the oldest kept and still a baseline; one more and it is gone, and the
    // snapshot is full.
    std::uint32_t step = 4;
    while (sender.sequence() < 3 + kSnapshotHistory - 1) {
        (void)sender.build(step++, humans);
    }
    CHECK(sender.build(step++, humans).baseline == 3); // sequence 3 + kSnapshotHistory
    const Snapshot full = sender.build(step++, humans);
    CHECK(full.baseline == 0);
    CHECK(full.humans.size() == 2);
    CHECK(sender.holding(3) == nullptr);
    CHECK_FALSE(sender.acknowledge(3).value()); // no longer kept
}

// Expected: unverified, #554 (Coney's own snapshots; the original has no network play)
TEST_CASE("snapshots: the client ignores stale snapshots and those whose baseline it lacks, and refuses broken ones",
          "[net][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    auto humans = scene(2, 0);
    (void)exchange(sender, receiver, 0, humans);
    const Snapshot old = sender.build(1, humans);
    const Snapshot newer = sender.build(2, humans);
    REQUIRE(receiver.apply(newer).value() == SnapshotApplied::Applied);
    CHECK(receiver.apply(old).value() == SnapshotApplied::Stale);
    CHECK(receiver.apply(newer).value() == SnapshotApplied::Stale); // a duplicate

    // A delta against a snapshot the client never decoded waits for a later one.
    SnapshotReceiver late;
    CHECK(late.apply(newer).value() == SnapshotApplied::NoBaseline);
    CHECK(late.newest() == 0);
    CHECK(late.humans().empty());

    // What decodes but breaks what its baseline allows is refused.
    const auto refused = [](const Snapshot& broken) {
        SnapshotReceiver fromEmpty;
        REQUIRE(fromEmpty.apply(Snapshot{.sequence = 1, .baseline = 0, .step = 0, .humans = {}}).has_value());
        const auto applied = fromEmpty.apply(broken);
        REQUIRE_FALSE(applied.has_value());
        CHECK(applied.error().code == ErrorCode::Invalid);
    };
    // The removal of a human the baseline does not hold.
    refused(Snapshot{.sequence = 2,
                     .baseline = 1,
                     .step = 1,
                     .humans = {SnapshotEntry{.id = idOf(4), .removed = true, .changed = 0, .words = {}}}});
    // A new human with a field left out.
    refused(Snapshot{.sequence = 2,
                     .baseline = 1,
                     .step = 1,
                     .humans = {SnapshotEntry{.id = idOf(4), .removed = false, .changed = 1, .words = {}}}});
    // A field "changed" to the value the baseline holds.
    SnapshotReceiver client;
    SnapshotSender fresh;
    REQUIRE(client.apply(fresh.build(0, humans)).value() == SnapshotApplied::Applied);
    HumanWords healthOnly{};
    healthOnly.at(at(HumanField::Health)) = humans[0].words.at(at(HumanField::Health));
    const auto same = client.apply(
        Snapshot{.sequence = 2,
                 .baseline = 1,
                 .step = 1,
                 .humans = {SnapshotEntry{
                     .id = idOf(0), .removed = false, .changed = 1U << at(HumanField::Health), .words = healthOnly}}});
    REQUIRE_FALSE(same.has_value());
    CHECK(same.error().code == ErrorCode::Invalid);
}

// Expected: unverified, #554 (Coney's own snapshot packet; the original has no network play)
TEST_CASE("snapshots: the packet is pinned, round-trips and refuses what breaks its rules", "[net][snapshot]") {
    // Id 10; sequence 3; baseline 1 back; step 7; one entry: slot 2 generation 1 (22 bits), not removed, then a
    // changed bit per field with only grounded (field 21, after the four animation layers' fields) set and its word, 1.
    const Snapshot small{.sequence = 3,
                         .baseline = 2,
                         .step = 7,
                         .humans = {SnapshotEntry{.id = NetId{.slot = 2, .generation = 1},
                                                  .removed = false,
                                                  .changed = 1U << at(HumanField::Grounded),
                                                  .words = [] {
                                                      HumanWords words{};
                                                      words.at(at(HumanField::Grounded)) = 1;
                                                      return words;
                                                  }()}}};
    // From bit 0: slot 2 (16 bits) = 0x02 0x00; generation 1 (6 bits), removed 0, field 0's changed bit 0 = 0x01;
    // fields 1 to 8 unchanged = 0x00; fields 9 to 16 unchanged = 0x00; fields 17 to 20 unchanged (bits 0 to 3), field
    // 21 changed and its word 1 (bits 4 and 5), field 22 unchanged = 0x30.
    const std::vector<std::byte> expected{std::byte{0x0a}, std::byte{0x03}, std::byte{0x01}, std::byte{0x07},
                                          std::byte{0x01}, std::byte{0x02}, std::byte{0x00}, std::byte{0x01},
                                          std::byte{0x00}, std::byte{0x00}, std::byte{0x30}};
    CHECK(encode(small) == expected);
    CHECK(overTheWire(small) == small);
    CHECK(encode(SnapshotAck{.sequence = 300}) ==
          std::vector<std::byte>{std::byte{0x0b}, std::byte{0xac}, std::byte{0x02}});
    CHECK(std::get<SnapshotAck>(decode(encode(SnapshotAck{.sequence = 9})).value()).sequence == 9);
    CHECK(deliveryOf(small) == coney::net::Delivery::UnreliableSequenced);
    CHECK(deliveryOf(SnapshotAck{.sequence = 1}) == coney::net::Delivery::Unreliable);
    CHECK(snapshotHeaderBits(small, 1) + snapshotEntryBits(small.humans[0]) <= encode(small).size() * 8);

    const auto refused = [](std::vector<std::byte> packet) {
        const auto decoded = decode(packet);
        REQUIRE_FALSE(decoded.has_value());
        CHECK(decoded.error().code == ErrorCode::Invalid);
    };
    // A baseline further back than the history, or before the first snapshot.
    refused({std::byte{0x0a}, std::byte{0x40}, std::byte{0x21}, std::byte{0x00}, std::byte{0x00}});
    refused({std::byte{0x0a}, std::byte{0x03}, std::byte{0x03}, std::byte{0x00}, std::byte{0x00}});
    // Sequence 0, and an acknowledgement of it.
    refused({std::byte{0x0a}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}});
    refused({std::byte{0x0b}, std::byte{0x00}});
    // More entries than the packet could hold.
    refused({std::byte{0x0a}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x05}, std::byte{0x00}});
    // An entry with nothing changed (slot 2, generation 1, not removed, no changed bits: 46 bits, 6 bytes).
    refused({std::byte{0x0a}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0x02},
             std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}});
    // The same entry twice, which breaks the rising order: count 2, then the small snapshot's entry written twice.
    std::vector<std::byte> twice{std::byte{0x0a}, std::byte{0x01}, std::byte{0x00}, std::byte{0x07}, std::byte{0x02}};
    BitWriter entries;
    for (int i = 0; i < 2; ++i) {
        writeNetId(entries, small.humans[0].id);
        entries.writeBool(false);
        for (std::size_t field = 0; field < kHumanFieldCount; ++field) {
            const bool changed = field == at(HumanField::Grounded);
            entries.writeBool(changed);
            if (changed) {
                entries.writeBits(1, 1);
            }
        }
    }
    twice.insert(twice.end(), entries.bytes().begin(), entries.bytes().end());
    refused(twice);
    // A word over its field's bits is refused too (a Bool's word of 2).
    CHECK(fieldValue(fieldTable(ReplicatedKind::Human)[at(HumanField::Grounded)], 2).error().code ==
          ErrorCode::Invalid);
}

// Expected: unverified, #554 (Coney's own snapshots; AnimTaskStack shows up to four layers at once, its port of the
// original's task stack, docs/research/formats/animation.md#task-stack)
TEST_CASE("snapshots: a human in chained fades carries all four of its animation layers", "[net][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    auto humans = scene(1, 0);
    (void)exchange(sender, receiver, 0, humans);

    // Three changes fade in over one another: under the newest layer, three older ones, each with the fade over it.
    std::array<FieldValue, kHumanFieldCount> values = humanValues(humans[0].words);
    for (int layer = 1; layer <= 3; ++layer) {
        const std::size_t clip = fieldNamed("clip" + std::to_string(layer));
        const std::size_t playhead = fieldNamed("playhead" + std::to_string(layer));
        const std::size_t fade = fieldNamed("fade" + std::to_string(layer));
        REQUIRE(clip < kHumanFieldCount);
        REQUIRE(playhead < kHumanFieldCount);
        REQUIRE(fade < kHumanFieldCount);
        values.at(clip) = std::int32_t{100 + layer};
        values.at(playhead) = 0.25F * static_cast<float>(layer);
        values.at(fade) = 0.125F * static_cast<float>(layer);
    }
    humans[0].words = humanWords(values);
    const Snapshot delta = exchange(sender, receiver, 1, humans);
    REQUIRE(delta.humans.size() == 1);
    CHECK(std::popcount(delta.humans[0].changed) == 9);
    CHECK(receiver.humans() == holdingOf(humans));
    const auto held = humanValues(receiver.humans().at(idOf(0)));
    for (int layer = 1; layer <= 3; ++layer) {
        CHECK(std::get<std::int32_t>(held.at(fieldNamed("clip" + std::to_string(layer)))) == 100 + layer);
    }
}

// Expected: unverified, #554 (Coney's own snapshots; a layer may be the locomotion's GaitBlendTask, whose 0 to 4 value
// picks and blends two of its five clips: src/animation/anim_task.h)
TEST_CASE("snapshots: a human's walk or run blend carries its gait value on every animation layer", "[net][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    auto humans = scene(1, 0);
    (void)exchange(sender, receiver, 0, humans);

    // Each layer between two gaits, at a value no whole number is near: the newest is "gait", the older ones "gait1"
    // to "gait3", beside each layer's clip and playhead.
    const std::array<std::string, 4> names{"gait", "gait1", "gait2", "gait3"};
    const std::array<float, 4> gaits{0.5F, 1.6F, 2.7F, 3.8F};
    std::array<FieldValue, kHumanFieldCount> values = humanValues(humans[0].words);
    for (std::size_t layer = 0; layer < names.size(); ++layer) {
        INFO(names.at(layer));
        const std::size_t gait = fieldNamed(names.at(layer));
        REQUIRE(gait < kHumanFieldCount);
        const FieldSpec& field = fieldTable(ReplicatedKind::Human)[gait];
        // The blend's whole range, and a step fine enough that the sampler's thresholds (the lower clip alone below a
        // fraction of 0.01, the upper above 0.995) fall on the same side on the guest as on the host.
        REQUIRE(field.encoding == FieldEncoding::Float);
        CHECK(field.min == 0.0F);
        CHECK(field.max == 4.0F);
        CHECK(fieldStep(field) / 2.0F < 0.005F);
        values.at(gait) = gaits.at(layer);
    }
    humans[0].words = humanWords(values);
    const Snapshot delta = exchange(sender, receiver, 1, humans);
    REQUIRE(delta.humans.size() == 1);
    CHECK(std::popcount(delta.humans[0].changed) == 4);
    CHECK(receiver.humans() == holdingOf(humans));
    const auto held = humanValues(receiver.humans().at(idOf(0)));
    for (std::size_t layer = 0; layer < names.size(); ++layer) {
        const std::size_t gait = fieldNamed(names.at(layer));
        REQUIRE(gait < kHumanFieldCount);
        const float step = fieldStep(fieldTable(ReplicatedKind::Human)[gait]);
        CHECK(std::abs(std::get<float>(held.at(gait)) - gaits.at(layer)) <= step / 2.0F);
    }
}

// Expected: unverified, #554 (Coney's own snapshots; the budget's arithmetic is in the comments)
TEST_CASE("snapshots: a busy scene's bytes per snapshot are pinned", "[net][snapshot]") {
    // Sixty humans, every fifth walking (twelve), as network-architecture.md#snapshots budgets them.
    SnapshotSender sender;
    SnapshotReceiver receiver;
    std::vector<std::size_t> bytes;
    std::vector<std::size_t> entries;
    for (int step = 0; step < 5; ++step) {
        const Snapshot snapshot = exchange(sender, receiver, static_cast<std::uint32_t>(step), scene(60, step, 5));
        bytes.push_back(encode(snapshot).size());
        entries.push_back(snapshot.humans.size());
    }
    // A human in full: id 22 bits, removed 1, 23 changed bits and the fields' 256 bits = 302 bits. The header: the id
    // byte and four one-byte varints = 40 bits. The first snapshot fits 31 of the 60: 40 + 31 x 302 = 9,402 bits, 1,176
    // bytes; a 32nd would take it past 9,600 bits (1,200 bytes).
    CHECK(entries[0] == 31);
    CHECK(bytes[0] == 1176);
    // The second: the 29 left out, in full (their priority grew to 2), and the 7 walkers among the first 31 (x,
    // heading and playhead changed: 23 + 23 + 18 + 10 + 10 = 84 bits): 40 + 29 x 302 + 7 x 84 = 9,386 bits, 1,174
    // bytes.
    CHECK(entries[1] == 36);
    CHECK(bytes[1] == 1174);
    // From then on, the 12 walkers alone: 40 + 12 x 84 = 1,048 bits, 131 bytes a snapshot, about 3.9 KB/s at 30 Hz.
    CHECK(entries[2] == 12);
    CHECK(bytes[2] == 131);
    CHECK(bytes[3] == 131);
    CHECK(bytes[4] == 131);
    CHECK(receiver.humans() == holdingOf(scene(60, 4, 5)));
}

// Expected: unverified, #554 (Coney's own priority accumulator; the original has no network play)
TEST_CASE("snapshots: the budget holds every packet to its bytes, and the priority accumulator reaches everyone",
          "[net][snapshot]") {
    // Two hundred humans all walking: about 1,850 bytes of change a step, so some wait every snapshot.
    SnapshotSender sender;
    SnapshotReceiver receiver;
    std::map<NetId, int> lastSent;
    std::map<NetId, int> longestWait;
    for (int step = 0; step < 60; ++step) {
        auto humans = scene(200, step + 1, 1);
        humans[150].priority = 50; // the one the client must see sooner: next to its player, say
        const Snapshot snapshot = exchange(sender, receiver, static_cast<std::uint32_t>(step), humans);
        CHECK(encode(snapshot).size() <= kSnapshotBudgetBytes);
        for (const SnapshotEntry& entry : snapshot.humans) {
            const auto before = lastSent.find(entry.id);
            const int waited = before == lastSent.end() ? step + 1 : step - before->second;
            longestWait[entry.id] = std::max(longestWait[entry.id], waited);
            lastSent[entry.id] = step;
        }
    }
    CHECK(lastSent.size() == 200);         // everyone has been sent
    CHECK(longestWait.at(idOf(150)) == 1); // the high-priority human goes in every snapshot
    // Nobody waits long: their priority grows until they go. The longest wait is the first one: a human in full is
    // 302 bits, so a snapshot holds 31 ((9,600 - 40) / 302), and the 200 first go out over seven snapshots.
    int longest = 0;
    for (const auto& [id, wait] : longestWait) {
        longest = std::max(longest, wait);
    }
    CHECK(longest == 7);
    // A human whose change waits holds a grown priority; one just sent holds none.
    const auto humans = scene(200, 61, 1);
    const Snapshot next = sender.build(60, humans);
    std::set<NetId> sent;
    for (const SnapshotEntry& entry : next.humans) {
        sent.insert(entry.id);
        CHECK(sender.priority(entry.id) == 0);
    }
    CHECK(sent.size() < 200);
    for (const SnapshotHuman& human : humans) {
        if (!sent.contains(human.id)) {
            CHECK(sender.priority(human.id) >= 1);
        }
    }
}

// Expected: unverified, #554 (Coney's own snapshots; the original has no network play)
TEST_CASE("snapshots: round trip under loss, reordering, duplicates and late acknowledgements", "[net][snapshot]") {
    // A server and a client with a bad link between them in both directions, by hand: each snapshot and each
    // acknowledgement may be lost, held back a few steps or arrive twice. Every snapshot the client decodes must leave
    // it holding exactly what the server's record of that snapshot says.
    SnapshotSender sender;
    SnapshotReceiver receiver;
    TestRandom random;
    struct InFlight {
        int arrives = 0;
        Snapshot snapshot;
    };
    std::vector<InFlight> down;                    // snapshots on their way
    std::vector<std::pair<int, std::uint32_t>> up; // acknowledgements on their way: when, and which
    std::size_t applied = 0;
    std::size_t deltas = 0;
    std::vector<SnapshotHuman> humans;
    for (int step = 0; step < 300; ++step) {
        // Humans come and go: thirty in a row, every third walking, one of them leaving for a while each 50 steps.
        humans = scene(30, step, 3);
        if ((step / 50) % 2 == 1) {
            humans.erase(humans.begin() + 7);
        }
        const bool clean = step >= 280; // the link heals at the end, so everything must arrive
        const Snapshot snapshot = sender.build(static_cast<std::uint32_t>(step), humans);
        deltas += snapshot.baseline != 0 ? 1 : 0;
        if (clean || !random.chance(25)) {
            const int delay = clean ? 0 : 1 + static_cast<int>(random.next() % 4);
            down.push_back(InFlight{.arrives = step + delay, .snapshot = overTheWire(snapshot)});
            if (!clean && random.chance(10)) {
                down.push_back(InFlight{.arrives = step + delay + 2, .snapshot = overTheWire(snapshot)});
            }
        }
        // What arrives this step, in the order it was held (so later-sent may come first).
        for (auto it = down.begin(); it != down.end();) {
            if (it->arrives != step) {
                ++it;
                continue;
            }
            const auto result = receiver.apply(it->snapshot);
            REQUIRE(result.has_value());
            if (*result == SnapshotApplied::Applied) {
                ++applied;
                const SnapshotHumans* record = sender.holding(it->snapshot.sequence);
                REQUIRE(record != nullptr);
                CHECK(receiver.humans() == *record);
                if (clean || !random.chance(25)) {
                    up.emplace_back(step + (clean ? 1 : 1 + static_cast<int>(random.next() % 4)),
                                    it->snapshot.sequence);
                }
            }
            it = down.erase(it);
        }
        for (auto it = up.begin(); it != up.end();) {
            if (it->first != step) {
                ++it;
                continue;
            }
            REQUIRE(sender.acknowledge(it->second).has_value());
            it = up.erase(it);
        }
    }
    CHECK(applied > 150);
    CHECK(deltas > 150);                             // most snapshots were deltas, despite the loss
    REQUIRE(receiver.newest() == sender.sequence()); // the last one arrived
    REQUIRE(sender.holding(receiver.newest()) != nullptr);
    CHECK(receiver.humans() == *sender.holding(receiver.newest()));
    // Healed, the client holds the server's humans as they are.
    CHECK(receiver.humans() == holdingOf(humans));
}
