// SPDX-License-Identifier: GPL-3.0-or-later
// The radar's blips in the snapshots (docs/guides/snapshots.md#hud-blips): hud::RadarBlip to its field table's words
// and back (the human it follows by network id, or a quantized place), the slot each blip keeps, their deltas,
// removals, budget order and priority, pinned bytes, the decoder's and the client's refusals, and the game's blips as
// network play captures them. The session over the loopback is in sessions_test.cpp. Synthetic values; no game data.
#include "net/radar_blips.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "hud/hud.h"
#include "hud/radar.h"
#include "net/bit_stream.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"
#include "net/snapshot.h"
#include "support/optional_value.h"

using Catch::Approx;
using coney::ErrorCode;
using coney::anim::Vec3;
using coney::hud::RadarBlip;
using namespace coney::net;

namespace {

// Field `f`'s index into a blip's words.
constexpr std::size_t at(HudBlipField f) { return static_cast<std::size_t>(f); }

// The word of field `f` for `value`.
std::uint32_t wordOf(HudBlipField f, FieldValue value) {
    return fieldWord(fieldTable(ReplicatedKind::HudBlip)[at(f)], value);
}

// The word a coordinate of the zero place has: what a blip with no place carries.
std::uint32_t zeroPlaceWord() { return wordOf(HudBlipField::PlaceX, FieldValue{0.0F}); }

// A mission objective that stands at (3, 4, 5): the kind a script adds with HUDAddRadarMissionObjective.
RadarBlip objective() {
    return RadarBlip{.type = 10,
                     .icon = 27,
                     .scale = 0.7F,
                     .colour = coney::graphics::Rgba{200, 20, 20, 255},
                     .flashCountdown = 100,
                     .place = Vec3{3.0F, 4.0F, 5.0F}};
}

// A human's blip as HUDAddRadarHuman makes it, scanned and shown.
RadarBlip humanBlip() {
    return RadarBlip{.type = 7,
                     .icon = 365,
                     .scale = 0.56F,
                     .colour = coney::graphics::kWhite,
                     .flashing = true,
                     .iconLock = true,
                     .marked = true,
                     .markTimer = 40,
                     .dotShown = true};
}

// A synthetic id.
NetId idOf(std::uint16_t slot, std::uint8_t generation = 1) { return NetId{.slot = slot, .generation = generation}; }

// `count` blips of a level, handles 1 to `count`, the last `objectives` of them mission objectives.
std::map<double, RadarBlip> level(std::size_t count, std::size_t objectives = 0) {
    std::map<double, RadarBlip> blips;
    for (std::size_t i = 0; i < count; ++i) {
        RadarBlip blip = i + objectives >= count ? objective() : humanBlip();
        blip.place = Vec3{static_cast<float>(i), 2.0F * static_cast<float>(i), 0.0F};
        blips.emplace(static_cast<double>(i + 1), blip);
    }
    return blips;
}

// What a client holds after the snapshot's blip entries, from the words of `blips` (by slot).
SnapshotBlips heldFrom(const std::vector<SnapshotBlip>& blips) {
    SnapshotBlips held;
    for (const SnapshotBlip& blip : blips) {
        held.emplace(blip.slot, blip.words);
    }
    return held;
}

// The snapshot as the client receives it: encoded, then decoded, which must succeed.
Snapshot overTheWire(const Snapshot& snapshot) {
    const auto decoded = decode(encode(snapshot));
    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<Snapshot>(*decoded));
    return std::get<Snapshot>(*decoded);
}

// Sends one snapshot of `blips` (and no humans or HUDs) from `sender` to `receiver`, which acknowledges it at once.
Snapshot exchange(SnapshotSender& sender, SnapshotReceiver& receiver, std::uint32_t step,
                  const std::vector<SnapshotBlip>& blips) {
    const Snapshot snapshot = sender.build(step, {}, {}, {}, {}, blips);
    const auto applied = receiver.apply(overTheWire(snapshot));
    REQUIRE(applied.has_value());
    REQUIRE(*applied == SnapshotApplied::Applied);
    REQUIRE(sender.acknowledge(snapshot.sequence).value());
    return snapshot;
}

// The slots of a snapshot's blip entries, in order.
std::vector<std::uint8_t> slotsOf(const Snapshot& snapshot) {
    std::vector<std::uint8_t> slots;
    slots.reserve(snapshot.blips.size());
    for (const SnapshotBlipEntry& entry : snapshot.blips) {
        slots.push_back(entry.slot);
    }
    return slots;
}

// A synthetic human in full, standing still: what the budget tests fill a snapshot with.
SnapshotHuman humanAt(std::uint16_t slot) {
    return SnapshotHuman{.id = NetId{.slot = slot, .generation = 1}, .words = {}, .priority = 1};
}

} // namespace

// Expected: unverified, #1537 (Coney's own field table; the blip's fields are the original's slot's,
// gui.md#radar-blip-slots)
TEST_CASE("radar blips: a blip's words say what it is, and a client's HUD gets it back", "[net][hud][radar]") {
    RadarBlip blip = humanBlip();
    blip.scale = 0.7F;
    blip.colour = coney::graphics::Rgba{1, 2, 3, 4};
    blip.flashCountdown = 63;
    blip.place = Vec3{-12.5F, 300.0F, 7.25F};
    const BlipWords words = blipWords(blip);
    CHECK(words.at(at(HudBlipField::Type)) == 7);
    CHECK(words.at(at(HudBlipField::Icon)) == 365);
    CHECK(words.at(at(HudBlipField::Red)) == 1);
    CHECK(words.at(at(HudBlipField::Alpha)) == 4);
    CHECK(words.at(at(HudBlipField::Flashing)) == 1);
    CHECK(words.at(at(HudBlipField::Blinking)) == 1);
    CHECK(words.at(at(HudBlipField::IconLock)) == 1);
    CHECK(words.at(at(HudBlipField::Marked)) == 1);
    CHECK(words.at(at(HudBlipField::Marking)) == 1);
    CHECK(words.at(at(HudBlipField::DotShown)) == 1);
    CHECK(words.at(at(HudBlipField::Follows)) == 0);
    CHECK(words.at(at(HudBlipField::HasPlace)) == 1);

    const RadarBlip back = radarBlipOf(words);
    CHECK(back.type == 7);
    CHECK(back.icon == 365);
    CHECK(back.scale == Approx(0.7F).margin(0.0002F));
    CHECK(back.colour == coney::graphics::Rgba{1, 2, 3, 4});
    CHECK(back.flashing);
    CHECK(back.iconLock);
    CHECK(back.marked);
    CHECK(back.dotShown);
    CHECK(back.follows == 0);
    // The running counts are each HUD's own: they come back at their start, since the guest's HUD takes the host's
    // each snapshot while they run.
    CHECK(back.flashCountdown == coney::hud::kRadarObjectiveFlash);
    CHECK(back.markTimer == coney::hud::kRadarMarkUpdates);
    // The place, within a step (0.125 m) of the host's.
    REQUIRE(back.fixedPlace.has_value());
    const Vec3 place = coney::test::got(back.fixedPlace);
    CHECK(place.x == Approx(-12.5F).margin(0.07F));
    CHECK(place.y == Approx(300.0F).margin(0.07F));
    CHECK(place.z == Approx(7.25F).margin(0.07F));
    // The settled state, the batch and the step's place are the guest's HUD's own.
    CHECK_FALSE(back.settled);
    CHECK_FALSE(back.place.has_value());

    // Counts that are not running travel as not running.
    RadarBlip idle = humanBlip();
    idle.markTimer = 0;
    idle.flashCountdown = 0;
    const BlipWords idleWords = blipWords(idle);
    CHECK(idleWords.at(at(HudBlipField::Blinking)) == 0);
    CHECK(idleWords.at(at(HudBlipField::Marking)) == 0);
    const RadarBlip idleBack = radarBlipOf(idleWords);
    CHECK(idleBack.flashCountdown == 0);
    CHECK(idleBack.markTimer == 0);
}

// Expected: unverified, #1537 (the choice written in snapshots.md#hud-blips: a human by id, else a place)
TEST_CASE("radar blips: a blip names the human it follows by id, else its place, else nothing", "[net][hud][radar]") {
    // Following a human: his id, and no place, whatever the blip last found.
    RadarBlip following = humanBlip();
    following.place = Vec3{10.0F, 20.0F, 30.0F};
    const NetId human = idOf(77, 3);
    const BlipWords byId = blipWords(following, human);
    CHECK(byId.at(at(HudBlipField::Follows)) == netIdNumber(human));
    CHECK(byId.at(at(HudBlipField::HasPlace)) == 0);
    CHECK(byId.at(at(HudBlipField::PlaceX)) == zeroPlaceWord());
    CHECK(byId.at(at(HudBlipField::PlaceY)) == zeroPlaceWord());
    CHECK(byId.at(at(HudBlipField::PlaceZ)) == zeroPlaceWord());
    const RadarBlip followed = radarBlipOf(byId);
    CHECK(followed.follows == netIdNumber(human));
    CHECK(netIdFromNumber(followed.follows) == human);
    CHECK_FALSE(followed.fixedPlace.has_value());

    // Following none: its fixed point first, else where the last radar step found it.
    RadarBlip fixed = humanBlip();
    fixed.fixedPlace = Vec3{1.0F, 2.0F, 3.0F};
    fixed.place = Vec3{50.0F, 50.0F, 50.0F};
    const RadarBlip fixedBack = radarBlipOf(blipWords(fixed));
    CHECK(coney::test::got(fixedBack.fixedPlace).x == Approx(1.0F).margin(0.07F));
    RadarBlip found = humanBlip();
    found.place = Vec3{50.0F, -40.0F, 0.0F};
    const RadarBlip foundBack = radarBlipOf(blipWords(found));
    CHECK(coney::test::got(foundBack.fixedPlace).y == Approx(-40.0F).margin(0.07F));
    CHECK(foundBack.follows == 0);

    // Not found by the host either: no place, and the words of one blip are the same whatever it last found.
    const BlipWords none = blipWords(humanBlip());
    CHECK(none.at(at(HudBlipField::HasPlace)) == 0);
    CHECK(none.at(at(HudBlipField::PlaceZ)) == zeroPlaceWord());
    CHECK_FALSE(radarBlipOf(none).fixedPlace.has_value());
}

// Expected: unverified, #1537 (Coney's choice of ranges, held at their ends as the other HUD fields are)
TEST_CASE("radar blips: a number past its field's range is held at the range's end", "[net][hud][radar]") {
    RadarBlip wide = humanBlip();
    wide.type = 40;
    wide.icon = 5000;
    wide.scale = 99.0F;
    wide.place = Vec3{5000.0F, -5000.0F, 1024.0F};
    const RadarBlip held = radarBlipOf(blipWords(wide));
    CHECK(held.type == 12);
    CHECK(held.icon == 1023);
    CHECK(held.scale == Approx(16.0F));
    CHECK(coney::test::got(held.fixedPlace).x == Approx(1024.0F));
    CHECK(coney::test::got(held.fixedPlace).y == Approx(-1024.0F));
    RadarBlip narrow = humanBlip();
    narrow.type = -3;
    narrow.icon = -1;
    narrow.scale = -2.0F;
    const RadarBlip low = radarBlipOf(blipWords(narrow));
    CHECK(low.type == 0);
    CHECK(low.icon == 0);
    CHECK(low.scale == Approx(0.0F));
}

// Expected: unverified, #1537 (Coney's own priority, snapshots.md#hud-blips)
TEST_CASE("radar blips: a mission objective's priority grows four times as fast as the rest's", "[net][hud][radar]") {
    CHECK(blipPriority(RadarBlip{.type = 10}) == 4);
    CHECK(blipPriority(RadarBlip{.type = 1}) == 4);
    for (const int type : {2, 3, 4, 6, 7, 8, 9, 12}) {
        CHECK(blipPriority(RadarBlip{.type = type}) == 1);
    }
}

// Expected: unverified, #1537 (Coney's own slots; the original's radar keeps 128, gui.md#radar-blip-slots)
TEST_CASE("radar blips: a blip keeps its slot while it lasts, a new one takes the lowest free, and 128 is the most",
          "[net][hud][radar]") {
    BlipSlots slots;
    std::map<double, RadarBlip> blips;
    for (const double handle : {9.0, 3.0, 5.0}) {
        blips.emplace(handle, humanBlip());
    }
    // New in one step: in handle order.
    auto given = slots.assign(blips);
    CHECK(given == std::map<double, std::uint8_t>{{3.0, 0}, {5.0, 1}, {9.0, 2}});
    // One gone and two new: the kept ones keep theirs, the new take the lowest free in handle order.
    blips.erase(5.0);
    blips.emplace(4.0, humanBlip());
    blips.emplace(1.0, humanBlip());
    given = slots.assign(blips);
    CHECK(given == std::map<double, std::uint8_t>{{1.0, 1}, {3.0, 0}, {4.0, 3}, {9.0, 2}});
    CHECK(slots.size() == 4);
    // Everything gone and new again: the slots start over.
    slots.clear();
    CHECK(slots.size() == 0);
    CHECK(slots.assign({{2.0, humanBlip()}}) == std::map<double, std::uint8_t>{{2.0, 0}});

    // 130 blips at once: the first 128 by handle have a slot and the other two none, until one is freed.
    BlipSlots crowded;
    const std::map<double, RadarBlip> many = level(130);
    given = crowded.assign(many);
    CHECK(given.size() == kMaxRadarBlips);
    CHECK(given.at(1.0) == 0);
    CHECK(given.at(128.0) == 127);
    CHECK_FALSE(given.contains(129.0));
    CHECK_FALSE(given.contains(130.0));
    std::map<double, RadarBlip> fewer = many;
    fewer.erase(40.0);
    given = crowded.assign(fewer);
    CHECK(given.size() == kMaxRadarBlips);
    CHECK(given.at(129.0) == 39);
    CHECK_FALSE(given.contains(130.0));
    CHECK(given.at(41.0) == 40);
}

// Expected: unverified, #1537 (Coney's own capture, snapshots.md#hud-blips)
TEST_CASE("radar blips: the game's blips are taken in slot order with the human each follows and their priority",
          "[net][hud][radar]") {
    std::map<double, RadarBlip> blips;
    blips.emplace(30.0, humanBlip());
    blips.emplace(10.0, objective());
    blips.emplace(20.0, humanBlip());
    BlipSlots slots;
    // Handle 30 follows a human the session holds an id for; the others do not.
    const NetId human = idOf(5);
    const std::vector<SnapshotBlip> taken =
        snapshotBlips(blips, slots, [human](double handle) { return handle == 30.0 ? human : NetId{}; });
    REQUIRE(taken.size() == 3);
    CHECK(taken[0].slot == 0);
    CHECK(taken[1].slot == 1);
    CHECK(taken[2].slot == 2);
    // Slot 0 is handle 10, the objective: its place, and a rate of 4.
    CHECK(taken[0].words.at(at(HudBlipField::Type)) == 10);
    CHECK(taken[0].words.at(at(HudBlipField::HasPlace)) == 1);
    CHECK(taken[0].priority == 4);
    CHECK(taken[1].words.at(at(HudBlipField::Follows)) == 0);
    CHECK(taken[2].words.at(at(HudBlipField::Follows)) == netIdNumber(human));
    CHECK(taken[2].priority == 1);
    // Without a lookup every blip names its place (or none).
    BlipSlots again;
    for (const SnapshotBlip& blip : snapshotBlips(blips, again)) {
        CHECK(blip.words.at(at(HudBlipField::Follows)) == 0);
    }
    // What a client holds comes back as the HUD's blips, by slot as a number.
    const std::map<double, RadarBlip> client = hudBlips(heldFrom(taken));
    REQUIRE(client.size() == 3);
    CHECK(client.at(0.0).type == 10);
    CHECK(client.at(0.0).flashCountdown == coney::hud::kRadarObjectiveFlash);
    CHECK(client.at(2.0).follows == netIdNumber(human));
    CHECK_FALSE(client.at(2.0).fixedPlace.has_value());
    CHECK(client.at(1.0).icon == 365);
}

// Expected: unverified, #1537 (Coney's own snapshots; the original has no network play)
TEST_CASE("radar blips: a snapshot carries them in full first, then only what changed, removed when gone",
          "[net][hud][radar][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    BlipSlots slots;
    std::map<double, RadarBlip> blips = level(3, 1);
    // Handle 3 follows a human from the fifth snapshot on; until then every blip names its place.
    const NetId human = idOf(9);
    bool following = false;
    const BlipFollowed followed = [&](double handle) { return following && handle == 3.0 ? human : NetId{}; };
    std::vector<SnapshotBlip> now = snapshotBlips(blips, slots, followed);
    // The first: every blip in full, which the client then holds.
    const Snapshot first = exchange(sender, receiver, 0, now);
    CHECK(slotsOf(first) == std::vector<std::uint8_t>{0, 1, 2});
    for (const SnapshotBlipEntry& entry : first.blips) {
        CHECK_FALSE(entry.removed);
        CHECK(entry.changed == kAllBlipFields);
    }
    CHECK(receiver.blips() == heldFrom(now));

    // Nothing changed: nothing sent.
    const Snapshot still = exchange(sender, receiver, 1, snapshotBlips(blips, slots, followed));
    CHECK(still.blips.empty());

    // One blip's icon changes: that blip's entry alone, with that field alone.
    blips.at(2.0).icon = 366;
    now = snapshotBlips(blips, slots, followed);
    const Snapshot icon = exchange(sender, receiver, 2, now);
    REQUIRE(icon.blips.size() == 1);
    CHECK(icon.blips[0].slot == 1);
    CHECK(icon.blips[0].changed == (1U << at(HudBlipField::Icon)));
    CHECK(icon.blips[0].words.at(at(HudBlipField::Icon)) == 366);
    CHECK(receiver.blips() == heldFrom(now));

    // A change too small to reach the field's next step costs nothing.
    blips.at(2.0).scale += 0.00002F;
    now = snapshotBlips(blips, slots, followed);
    CHECK(exchange(sender, receiver, 3, now).blips.empty());

    // A blip that follows a human instead of standing still: its id, its place cleared and the flag, together.
    following = true;
    now = snapshotBlips(blips, slots, followed);
    const Snapshot follow = exchange(sender, receiver, 4, now);
    REQUIRE(follow.blips.size() == 1);
    CHECK(follow.blips[0].slot == 2);
    CHECK((follow.blips[0].changed & (1U << at(HudBlipField::Follows))) != 0);
    CHECK((follow.blips[0].changed & (1U << at(HudBlipField::HasPlace))) != 0);
    CHECK((follow.blips[0].changed & (1U << at(HudBlipField::Type))) == 0);

    // A blip gone: a removal, and its slot free for the next.
    blips.erase(2.0);
    now = snapshotBlips(blips, slots, followed);
    const Snapshot gone = exchange(sender, receiver, 5, now);
    REQUIRE(gone.blips.size() == 1);
    CHECK(gone.blips[0].slot == 1);
    CHECK(gone.blips[0].removed);
    CHECK(gone.blips[0].changed == 0);
    CHECK(receiver.blips().size() == 2);
    blips.emplace(9.0, humanBlip());
    now = snapshotBlips(blips, slots, followed);
    const Snapshot reused = exchange(sender, receiver, 6, now);
    REQUIRE(reused.blips.size() == 1);
    CHECK(reused.blips[0].slot == 1);
    CHECK(reused.blips[0].changed == kAllBlipFields);
    CHECK(receiver.blips() == heldFrom(now));
}

// Expected: unverified, #1537 (Coney's own snapshots; the baseline's rules are the humans' and HUDs',
// snapshots.md#model)
TEST_CASE("radar blips: a delta against an older baseline carries what that baseline lacks",
          "[net][hud][radar][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    BlipSlots slots;
    std::map<double, RadarBlip> blips = level(2);
    const std::vector<SnapshotBlip> first = snapshotBlips(blips, slots);
    exchange(sender, receiver, 0, first);
    // Two snapshots the client decodes but the server never sees acknowledged: the baseline stays the first.
    blips.at(1.0).icon = 400;
    const Snapshot second = sender.build(1, {}, {}, {}, {}, snapshotBlips(blips, slots));
    REQUIRE(receiver.apply(overTheWire(second)).has_value());
    blips.at(2.0).colour = coney::graphics::Rgba{9, 9, 9, 9};
    const std::vector<SnapshotBlip> third = snapshotBlips(blips, slots);
    const Snapshot delta = sender.build(2, {}, {}, {}, {}, third);
    CHECK(delta.baseline == 1);
    CHECK(slotsOf(delta) == std::vector<std::uint8_t>{0, 1});
    REQUIRE(receiver.apply(overTheWire(delta)).has_value());
    CHECK(receiver.blips() == heldFrom(third));
}

// Expected: unverified, #1537 (Coney's own wire; pins the entry's bits and a snapshot's bytes)
TEST_CASE("radar blips: an entry's bits and a snapshot's bytes are pinned, and round-trip",
          "[net][hud][radar][snapshot]") {
    // A blip in full: its slot (7) and removed bit, a changed bit for each of the 18 fields, and the words: type 4,
    // icon 10, size 16, colour 4 x 8, six flags, the human's id 22, has-place 1 and the place 3 x 14.
    SnapshotBlipEntry full{.slot = 5, .removed = false, .changed = kAllBlipFields, .words = blipWords(humanBlip())};
    CHECK(kBlipFieldCount == 18);
    CHECK(fieldsBits(fieldTable(ReplicatedKind::HudBlip)) == 4 + 10 + 16 + 32 + 6 + 22 + 1 + 42);
    CHECK(snapshotBlipEntryBits(full) == 7 + 1 + 18 + (4 + 10 + 16 + 32 + 6 + 22 + 1 + 42));
    CHECK(snapshotBlipEntryBits(SnapshotBlipEntry{.slot = 5, .removed = true}) == 8);
    // An icon changed: the slot, the removed bit, 18 changed bits and the icon's 10.
    SnapshotBlipEntry icon{.slot = 5, .changed = 1U << at(HudBlipField::Icon)};
    icon.words.at(at(HudBlipField::Icon)) = 366;
    CHECK(snapshotBlipEntryBits(icon) == 7 + 1 + 18 + 10);

    // The bytes: id 10; sequence 3; baseline 1 back; step 7; no humans, HUDs, elements or texts; one blip, slot 5,
    // whose icon changed to 366 (0x16e).
    const Snapshot small{.sequence = 3,
                         .baseline = 2,
                         .step = 7,
                         .humans = {},
                         .huds = {},
                         .hudElements = {},
                         .hudTexts = {},
                         .blips = {icon}};
    const std::vector<std::byte> bytes = encode(small);
    // Message id, three varints and four counts of zero, then the blip's count (1).
    const std::vector<std::byte> head{std::byte{0x0a}, std::byte{0x03}, std::byte{0x01},
                                      std::byte{0x07}, std::byte{0x00}, std::byte{0x00},
                                      std::byte{0x00}, std::byte{0x00}, std::byte{0x01}};
    REQUIRE(bytes.size() == head.size() + ((7 + 1 + 18 + 10 + 7) / 8));
    CHECK(std::equal(head.begin(), head.end(), bytes.begin()));
    // Then the slot 5 (7 bits) and the removed bit, field 0 (the type) unchanged, field 1 (the icon) changed with its
    // word 366 in 10 bits straight after, and the other 16 fields unchanged.
    BitWriter expectedTail;
    expectedTail.writeBits(5, 7);
    expectedTail.writeBool(false);
    expectedTail.writeBool(false);
    expectedTail.writeBool(true);
    expectedTail.writeBits(366, 10);
    for (std::size_t field = 2; field < kBlipFieldCount; ++field) {
        expectedTail.writeBool(false);
    }
    const std::vector<std::byte> tail(expectedTail.bytes().begin(), expectedTail.bytes().end());
    CHECK(std::equal(tail.begin(), tail.end(), bytes.begin() + static_cast<std::ptrdiff_t>(head.size())));
    CHECK(overTheWire(small) == small);

    // A snapshot of every blip in full of a busy level: pinned size, 40 blips of 159 bits.
    SnapshotBlipEntry entry{.slot = 0, .removed = false, .changed = kAllBlipFields, .words = blipWords(objective())};
    CHECK(snapshotBlipEntryBits(entry) == 159);
    // The header's bits count the blips' count too.
    CHECK(snapshotHeaderBits(small, 0, 0, 0, 0, 1) == 8 + 8 + 8 + 8 + 8 + 8 + 8 + 8 + 8);
}

// Expected: unverified, #1537 (Coney's own decoder: bytes come from the network)
TEST_CASE("radar blips: the decoder refuses what breaks the blips' rules", "[net][hud][radar][snapshot]") {
    const auto refused = [](const std::vector<std::byte>& packet) {
        const auto decoded = decode(packet);
        REQUIRE_FALSE(decoded.has_value());
        CHECK(decoded.error().code == ErrorCode::Invalid);
    };
    // The header of a snapshot with no humans, HUDs, elements or texts, then what `tail` writes: the blips.
    const auto packet = [](const std::function<void(BitWriter&)>& tail) {
        BitWriter out;
        for (const std::uint32_t b : {0x0aU, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}) {
            out.writeBits(b, 8);
        }
        tail(out);
        return std::vector<std::byte>(out.bytes().begin(), out.bytes().end());
    };
    // Blip `slot` with the icon changed to `word`, or with nothing changed; `type` first when given.
    const auto blip = [](BitWriter& out, std::uint32_t slot, bool changes, std::uint32_t word = 366) {
        out.writeBits(slot, kRadarBlipSlotBits);
        out.writeBool(false);
        for (std::size_t field = 0; field < kBlipFieldCount; ++field) {
            const bool changed = changes && field == at(HudBlipField::Icon);
            out.writeBool(changed);
            if (changed) {
                out.writeBits(word, 10);
            }
        }
    };
    // A clean blip decodes.
    CHECK(decode(packet([&](BitWriter& out) {
              out.writeVarUint(1);
              blip(out, 5, true);
          })).has_value());
    // More blips than there are slots.
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(kMaxRadarBlips + 1);
        blip(out, 5, true);
    }));
    // A blip with nothing changed.
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(1);
        blip(out, 5, false);
    }));
    // Slots not rising, and repeated.
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(2);
        blip(out, 6, true);
        blip(out, 5, true);
    }));
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(2);
        blip(out, 5, true);
        blip(out, 5, true);
    }));
    // A type past the original's last (field 0 changed to 13, which its 4 bits hold).
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(1);
        out.writeBits(5, kRadarBlipSlotBits);
        out.writeBool(false);
        out.writeBool(true);
        out.writeBits(13, 4);
        for (std::size_t field = 1; field < kBlipFieldCount; ++field) {
            out.writeBool(false);
        }
    }));
    // A count the packet cannot hold is refused before anything is allocated for it.
    refused(packet([&](BitWriter& out) { out.writeVarUint(100); }));
}

// Expected: unverified, #1537 (Coney's own snapshots; the client's rules are the HUDs', snapshots.md#packet)
TEST_CASE("radar blips: the client refuses a removal or change its baseline does not allow, and words that break a "
          "blip's rules",
          "[net][hud][radar][snapshot]") {
    const auto snapshotOf = [](std::uint32_t sequence, std::vector<SnapshotBlipEntry> blips) {
        return Snapshot{.sequence = sequence,
                        .baseline = sequence - 1,
                        .step = sequence,
                        .humans = {},
                        .huds = {},
                        .hudElements = {},
                        .hudTexts = {},
                        .blips = std::move(blips)};
    };
    const auto invalid = [](const std::expected<SnapshotApplied, coney::Error>& applied) {
        REQUIRE_FALSE(applied.has_value());
        CHECK(applied.error().code == ErrorCode::Invalid);
    };
    const BlipWords words = blipWords(humanBlip());
    SnapshotReceiver receiver;
    // A removal of a blip the baseline does not hold.
    invalid(receiver.apply(snapshotOf(1, {SnapshotBlipEntry{.slot = 3, .removed = true}})));
    // A new blip with fields left out.
    invalid(receiver.apply(snapshotOf(1, {SnapshotBlipEntry{.slot = 3, .changed = 1U << at(HudBlipField::Icon)}})));
    // Held; then a field "changed" to the word it has.
    REQUIRE(receiver.apply(snapshotOf(1, {SnapshotBlipEntry{.slot = 3, .changed = kAllBlipFields, .words = words}}))
                .has_value());
    SnapshotBlipEntry same{.slot = 3, .changed = 1U << at(HudBlipField::Icon)};
    same.words.at(at(HudBlipField::Icon)) = words.at(at(HudBlipField::Icon));
    invalid(receiver.apply(snapshotOf(2, {same})));
    // A blip that follows a human and also has a place, and one with no place and a place's word that is not the zero
    // place's: one blip has one set of words.
    BlipWords both = words;
    both.at(at(HudBlipField::Follows)) = netIdNumber(idOf(4));
    both.at(at(HudBlipField::HasPlace)) = 1;
    invalid(receiver.apply(snapshotOf(2, {SnapshotBlipEntry{.slot = 4, .changed = kAllBlipFields, .words = both}})));
    BlipWords stray = words;
    stray.at(at(HudBlipField::HasPlace)) = 0;
    stray.at(at(HudBlipField::PlaceX)) = zeroPlaceWord() + 1;
    invalid(receiver.apply(snapshotOf(2, {SnapshotBlipEntry{.slot = 4, .changed = kAllBlipFields, .words = stray}})));
    // A follows number with generation 0 is no id, so not a blip that follows.
    BlipWords noId = words;
    noId.at(at(HudBlipField::Follows)) = 5;
    noId.at(at(HudBlipField::HasPlace)) = 0;
    invalid(receiver.apply(snapshotOf(2, {SnapshotBlipEntry{.slot = 4, .changed = kAllBlipFields, .words = noId}})));
    // The client still holds what it did.
    CHECK(receiver.blips().size() == 1);
    CHECK(receiver.newest() == 1);
}

// Expected: unverified, #1537 (Coney's own budget order and priority, snapshots.md#hud-blips and #budget)
TEST_CASE("radar blips: they take at most a quarter of the budget, the objectives first, and none waits for ever",
          "[net][hud][radar][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    BlipSlots slots;
    // Sixty blips, the last three of them mission objectives (slots 57 to 59), and sixty humans to share the room.
    const std::map<double, RadarBlip> blips = level(60, 3);
    const std::vector<SnapshotBlip> now = snapshotBlips(blips, slots);
    std::vector<SnapshotHuman> humans;
    for (std::uint16_t slot = 1; slot <= 60; ++slot) {
        humans.push_back(humanAt(slot));
    }
    const std::size_t blipBits = snapshotBlipEntryBits(SnapshotBlipEntry{.slot = 0, .changed = kAllBlipFields});
    const std::size_t room = (kSnapshotBudgetBytes * 8 / kSnapshotBlipShare) / blipBits;
    REQUIRE(room < now.size());

    const Snapshot first = sender.build(0, humans, {}, {}, {}, now);
    // As many as the quarter holds, the objectives first by their rate (so they go though they come last by slot), and
    // the humans have the rest of the packet.
    CHECK(first.blips.size() == room);
    const std::vector<std::uint8_t> sentFirst = slotsOf(first);
    CHECK(std::ranges::find(sentFirst, 57) != sentFirst.end());
    CHECK(std::ranges::find(sentFirst, 58) != sentFirst.end());
    CHECK(std::ranges::find(sentFirst, 59) != sentFirst.end());
    CHECK(sentFirst.front() == 0);
    CHECK(std::ranges::find(sentFirst, static_cast<std::uint8_t>(room - 3)) == sentFirst.end());
    CHECK_FALSE(first.humans.empty());
    CHECK(encode(first).size() <= kSnapshotBudgetBytes);
    REQUIRE(receiver.apply(overTheWire(first)).has_value());
    REQUIRE(sender.acknowledge(first.sequence).value());
    // What waited has grown a step's priority; what went has none.
    CHECK(sender.blipPriority(0) == 0);
    CHECK(sender.blipPriority(57) == 0);
    CHECK(sender.blipPriority(static_cast<std::uint8_t>(room - 3)) == 1);

    // Each snapshot after takes the longest-waiting; every blip is held within the snapshots the quarter needs.
    std::uint32_t step = 1;
    while (receiver.blips().size() < now.size() && step < 10) {
        const Snapshot next = sender.build(step++, humans, {}, {}, {}, now);
        REQUIRE(receiver.apply(overTheWire(next)).has_value());
        REQUIRE(sender.acknowledge(next.sequence).value());
        CHECK(encode(next).size() <= kSnapshotBudgetBytes);
    }
    CHECK(receiver.blips() == heldFrom(now));
    CHECK(step <= 1 + (now.size() + room - 1) / room);
}

// Expected: unverified, #1537 (Coney's own budget order, snapshots.md#budget)
TEST_CASE("radar blips: a removal always goes, and a higher rate goes before the blips that come first by slot",
          "[net][hud][radar][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    // 40 blips the client holds already (a full entry each is more than the quarter holds, so a few snapshots).
    BlipSlots slots;
    std::map<double, RadarBlip> blips = level(40);
    std::uint32_t step = 0;
    while (receiver.blips().size() < 40 && step < 10) {
        exchange(sender, receiver, step++, snapshotBlips(blips, slots));
    }
    REQUIRE(receiver.blips().size() == 40);
    // The first blip is gone, and every other changes its colour, icon and size (84 bits each: more than the quarter
    // holds), the last becoming a mission objective.
    blips.erase(1.0);
    for (auto& [handle, blip] : blips) {
        blip.colour = coney::graphics::Rgba{7, 7, 7, 7};
        blip.icon = 100 + static_cast<int>(handle);
        blip.scale = 1.5F;
    }
    blips.at(40.0).type = 10;
    const Snapshot next = sender.build(step, {}, {}, {}, {}, snapshotBlips(blips, slots));
    const std::vector<std::uint8_t> sent = slotsOf(next);
    REQUIRE_FALSE(sent.empty());
    // The removal, its slot 0 first.
    CHECK(sent.front() == 0);
    CHECK(next.blips.front().removed);
    // The objective (slot 39, last by slot) is in; the blips that wait are the last of the rest, not the first.
    CHECK(sent.back() == 39);
    CHECK(std::ranges::find(sent, 38) == sent.end());
    CHECK(std::ranges::find(sent, 1) != sent.end());
    std::size_t bits = 0;
    for (const SnapshotBlipEntry& entry : next.blips) {
        bits += snapshotBlipEntryBits(entry);
    }
    CHECK(bits <= kSnapshotBudgetBytes * 8 / kSnapshotBlipShare);
}
