// SPDX-License-Identifier: GPL-3.0-or-later
// Each player slot's HUD state in the snapshots (docs/guides/snapshots.md#hud-state): hud::PlayerHudState to its field
// table's words and back, the HUD entries' deltas, removals and pinned bytes, and the game's HUDs as network play
// captures them. The session over the loopback is in sessions_test.cpp. Synthetic values; no game data.
#include "net/player_huds.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "hud/hud.h"
#include "net/bit_stream.h"
#include "net/game_humans.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"
#include "net/snapshot.h"

using coney::ErrorCode;
using coney::hud::PlayerHudState;
using namespace coney::net;

namespace {

// Field `f`'s index into a HUD's words.
constexpr std::size_t at(HudField f) { return static_cast<std::size_t>(f); }

// Player 1's state at the start of a level: some rage, a little money and a flash.
PlayerHudState firstPlayer() {
    PlayerHudState state;
    state.panel.rage = 12;
    state.panel.rageMax = 78;
    state.panel.score = 300;
    state.panel.money = 25;
    state.panel.items = {1, 0, 0, 0};
    return state;
}

// Player 2's: raging, with spray paint and handcuffs, his camera locked.
PlayerHudState secondPlayer() {
    PlayerHudState state;
    state.panel.rage = 90;
    state.panel.rageMax = 90;
    state.panel.raging = true;
    state.panel.altBanner = true;
    state.panel.score = 1250;
    state.panel.money = 7;
    state.panel.items = {0, 3, 2, 0};
    state.panel.selectShows = false;
    state.cameraIgnoresStick = true;
    return state;
}

// The snapshot as the client receives it: encoded, then decoded, which must succeed.
Snapshot overTheWire(const Snapshot& snapshot) {
    const auto decoded = decode(encode(snapshot));
    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<Snapshot>(*decoded));
    return std::get<Snapshot>(*decoded);
}

// Sends one snapshot of `huds` (and no humans) from `sender` to `receiver`, which acknowledges it at once.
Snapshot exchange(SnapshotSender& sender, SnapshotReceiver& receiver, std::uint32_t step,
                  const std::vector<SnapshotHud>& huds) {
    const Snapshot snapshot = sender.build(step, {}, huds);
    const auto applied = receiver.apply(overTheWire(snapshot));
    REQUIRE(applied.has_value());
    REQUIRE(*applied == SnapshotApplied::Applied);
    REQUIRE(sender.acknowledge(snapshot.sequence).value());
    return snapshot;
}

// The HUDs of `slots` as a client holds them.
SnapshotHuds holdingOf(const std::vector<PlayerHudState>& slots) {
    SnapshotHuds holding;
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        holding.emplace(static_cast<std::uint8_t>(slot), playerHudWords(slots[slot]));
    }
    return holding;
}

} // namespace

// Expected: unverified, #1447 (Coney's own HUD field table; the fields are hud.md#the-player-panel's)
TEST_CASE("player HUDs: a slot's state round-trips through its words, a number past its range held at the end",
          "[net][hud]") {
    const PlayerHudState second = secondPlayer();
    CHECK(playerHudState(playerHudWords(second)) == second);
    PlayerHudState knocked = firstPlayer();
    knocked.knockedOut = true;
    CHECK(playerHudState(playerHudWords(knocked)) == knocked);
    // The panel works out promptWakes from the prompt itself: it does not travel.
    PlayerHudState woken = firstPlayer();
    woken.panel.promptWakes = true;
    CHECK(playerHudState(playerHudWords(woken)) == firstPlayer());
    // Past the ranges: the money's s16, the counts' byte, the score's 25 bits.
    PlayerHudState rich = firstPlayer();
    rich.panel.money = 40000;
    rich.panel.items = {300, -2, 9, 255};
    rich.panel.score = -20000000;
    const PlayerHudState held = playerHudState(playerHudWords(rich));
    CHECK(held.panel.money == 32767);
    CHECK(held.panel.items == std::array<int, 4>{255, 0, 9, 255});
    CHECK(held.panel.score == -16777216);
    // The words are the field table's, field by field.
    const HudWords words = playerHudWords(second);
    const auto fields = fieldTable(ReplicatedKind::PlayerHud);
    CHECK(words.at(at(HudField::Rage)) == fieldWord(fields[at(HudField::Rage)], std::int32_t{90}));
    CHECK(words.at(at(HudField::Raging)) == 1);
    CHECK(words.at(at(HudField::SelectShows)) == 0);
    CHECK(words.at(at(HudField::CameraIgnoresStick)) == 1);
    CHECK(hudValues(words).at(at(HudField::Money)) == FieldValue{std::int32_t{7}});
}

// Expected: unverified, #1447 (Coney's own snapshots; the original has no network play)
TEST_CASE("player HUDs: the game's slots are listed in slot order, and kept only while capture is on", "[net][hud]") {
    const std::vector<PlayerHudState> slots{firstPlayer(), secondPlayer()};
    const std::vector<SnapshotHud> listed = snapshotHuds(slots);
    REQUIRE(listed.size() == 2);
    CHECK(listed[0].slot == 0);
    CHECK(listed[0].words == playerHudWords(slots[0]));
    CHECK(listed[1].slot == 1);
    CHECK(listed[1].words == playerHudWords(slots[1]));

    GameHumans game;
    game.captureHuds(slots);
    REQUIRE(game.huds().size() == 2);
    CHECK(game.huds()[1].words == playerHudWords(slots[1]));
    // A level going takes them; with capture off nothing is taken.
    game.clear();
    CHECK(game.huds().empty());
    game.captureHuds(slots);
    game.setCapturing(false);
    CHECK(game.huds().empty());
    game.captureHuds(slots);
    CHECK(game.huds().empty());
}

// Expected: unverified, #1447 (Coney's own snapshots; the original has no network play)
TEST_CASE("player HUDs: a snapshot carries each slot in full first, then only the fields that changed, and a slot gone "
          "is removed",
          "[net][hud][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    std::vector<PlayerHudState> slots{firstPlayer(), secondPlayer()};

    // The first: both slots with every field.
    const Snapshot first = exchange(sender, receiver, 0, snapshotHuds(slots));
    REQUIRE(first.huds.size() == 2);
    CHECK(first.huds[0].slot == 0);
    CHECK(first.huds[0].changed == kAllHudFields);
    CHECK(first.huds[1].changed == kAllHudFields);
    CHECK(receiver.huds() == holdingOf(slots));
    CHECK(*sender.holdingHuds(first.sequence) == holdingOf(slots));

    // Nothing changed: no HUD entry.
    const Snapshot still = exchange(sender, receiver, 1, snapshotHuds(slots));
    CHECK(still.huds.empty());
    CHECK(still.baseline == first.sequence);

    // Player 2 picks up money and his rage drains: those two fields of his slot alone.
    slots[1].panel.money = 57;
    slots[1].panel.rage = 85;
    const Snapshot delta = exchange(sender, receiver, 2, snapshotHuds(slots));
    REQUIRE(delta.huds.size() == 1);
    CHECK(delta.huds[0].slot == 1);
    CHECK(delta.huds[0].changed == ((1U << at(HudField::Money)) | (1U << at(HudField::Rage))));
    CHECK(snapshotHudEntryBits(delta.huds[0]) == 2 + 1 + kHudFieldCount + 16 + 16);
    CHECK(receiver.huds() == holdingOf(slots));
    CHECK(playerHudState(receiver.huds().at(1)) == slots[1]);

    // Player 2 leaves: his slot is removed, and comes back in full.
    const Snapshot left = exchange(sender, receiver, 3, snapshotHuds(std::vector<PlayerHudState>{slots[0]}));
    REQUIRE(left.huds.size() == 1);
    CHECK(left.huds[0].slot == 1);
    CHECK(left.huds[0].removed);
    CHECK(receiver.huds().size() == 1);
    const Snapshot back = exchange(sender, receiver, 4, snapshotHuds(slots));
    REQUIRE(back.huds.size() == 1);
    CHECK(back.huds[0].changed == kAllHudFields);
    CHECK(receiver.huds() == holdingOf(slots));
    CHECK(*receiver.holdingHuds(back.sequence) == *sender.holdingHuds(back.sequence));

    // A lost acknowledgement: the next delta goes against the older baseline and still leaves the right HUDs.
    slots[0].panel.score = 400;
    const Snapshot unacknowledged = sender.build(5, {}, snapshotHuds(slots));
    REQUIRE(receiver.apply(overTheWire(unacknowledged)).has_value());
    slots[0].panel.items = {2, 0, 0, 0};
    const Snapshot next = sender.build(6, {}, snapshotHuds(slots));
    CHECK(next.baseline == back.sequence);
    REQUIRE(next.huds.size() == 1);
    CHECK(next.huds[0].changed == ((1U << at(HudField::Score)) | (1U << at(HudField::Flash))));
    REQUIRE(receiver.apply(overTheWire(next)).has_value());
    CHECK(receiver.huds() == holdingOf(slots));
}

// Expected: unverified, #1447 (Coney's own snapshot packet; the original has no network play)
TEST_CASE("player HUDs: the HUD entries' bytes are pinned, round-trip and refuse what breaks their rules",
          "[net][hud][snapshot]") {
    HudWords money{};
    money.at(at(HudField::Money)) =
        fieldWord(fieldTable(ReplicatedKind::PlayerHud)[at(HudField::Money)], std::int32_t{25});
    REQUIRE(money.at(at(HudField::Money)) == 0x8019); // 25 from the s16 range's start, -32,768
    const Snapshot small{
        .sequence = 3,
        .baseline = 2,
        .step = 7,
        .humans = {},
        .huds = {SnapshotHudEntry{.slot = 1, .removed = false, .changed = 1U << at(HudField::Money), .words = money}}};
    // Id 10; sequence 3; baseline 1 back; step 7; no humans; one HUD entry. From its first bit: slot 1 (2 bits),
    // removed 0, fields 0 to 4 unchanged = 0x01; money's changed bit and its word's low 7 bits (0x19) = 0x33; the
    // word's next 8 bits = 0x00; its top bit, then fields 6 to 12 unchanged = 0x01.
    const std::vector<std::byte> expected{std::byte{0x0a}, std::byte{0x03}, std::byte{0x01}, std::byte{0x07},
                                          std::byte{0x00}, std::byte{0x01}, std::byte{0x01}, std::byte{0x33},
                                          std::byte{0x00}, std::byte{0x01}};
    CHECK(encode(small) == expected);
    CHECK(overTheWire(small) == small);
    CHECK(snapshotHeaderBits(small, 0, 1) + snapshotHudEntryBits(small.huds[0]) == expected.size() * 8);

    const auto refused = [](const std::vector<std::byte>& packet) {
        const auto decoded = decode(packet);
        REQUIRE_FALSE(decoded.has_value());
        CHECK(decoded.error().code == ErrorCode::Invalid);
    };
    // The header of a snapshot with no humans, then `count` HUD entries written as `entries`.
    const auto withHuds = [](std::uint64_t count, const BitWriter& entries) {
        std::vector<std::byte> packet{std::byte{0x0a}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
                                      std::byte{0x00}};
        BitWriter tail;
        tail.writeVarUint(count);
        for (const std::byte b : entries.bytes()) {
            tail.writeBits(static_cast<std::uint32_t>(b), 8);
        }
        packet.insert(packet.end(), tail.bytes().begin(), tail.bytes().end());
        return packet;
    };
    // An entry for slot `slot` that changes money to its word for 25, or nothing at all.
    const auto entry = [&money](BitWriter& out, std::uint32_t slot, bool changesMoney) {
        out.writeBits(slot, kPlayerSlotBits);
        out.writeBool(false);
        for (std::size_t field = 0; field < kHudFieldCount; ++field) {
            const bool changed = changesMoney && field == at(HudField::Money);
            out.writeBool(changed);
            if (changed) {
                out.writeBits(money.at(field), 16);
            }
        }
    };
    // The packet a clean entry makes decodes.
    BitWriter one;
    entry(one, 2, true);
    CHECK(decode(withHuds(1, one)).has_value());
    // More HUD entries than there are player slots.
    refused(withHuds(5, one));
    // An entry with nothing changed.
    BitWriter none;
    entry(none, 2, false);
    refused(withHuds(1, none));
    // Slots not rising: the same slot twice, and 3 before 1.
    BitWriter twice;
    entry(twice, 2, true);
    entry(twice, 2, true);
    refused(withHuds(2, twice));
    BitWriter falling;
    entry(falling, 3, true);
    entry(falling, 1, true);
    refused(withHuds(2, falling));
    // A Bool word over its bit can only be forged, so a word over a range is checked as for humans: a count of 256.
    CHECK(fieldValue(fieldTable(ReplicatedKind::PlayerHud)[at(HudField::Flash)], 256).error().code ==
          ErrorCode::Invalid);
}

// Expected: unverified, #1447 (Coney's own snapshots; the original has no network play)
TEST_CASE("player HUDs: the client refuses a HUD removal or a change its baseline does not allow",
          "[net][hud][snapshot]") {
    SnapshotReceiver receiver;
    const HudWords words = playerHudWords(firstPlayer());
    // A removal of a slot the baseline does not hold.
    const auto removal =
        receiver.apply(Snapshot{.sequence = 1,
                                .baseline = 0,
                                .step = 0,
                                .humans = {},
                                .huds = {SnapshotHudEntry{.slot = 0, .removed = true, .changed = 0, .words = {}}}});
    REQUIRE_FALSE(removal.has_value());
    CHECK(removal.error().code == ErrorCode::Invalid);
    // A new slot with fields left out.
    HudWords partial{};
    partial.at(at(HudField::Rage)) = words.at(at(HudField::Rage));
    const auto leftOut = receiver.apply(
        Snapshot{.sequence = 1,
                 .baseline = 0,
                 .step = 0,
                 .humans = {},
                 .huds = {SnapshotHudEntry{
                     .slot = 0, .removed = false, .changed = 1U << at(HudField::Rage), .words = partial}}});
    REQUIRE_FALSE(leftOut.has_value());
    // A field "changed" to the word it has.
    REQUIRE(receiver
                .apply(Snapshot{
                    .sequence = 1,
                    .baseline = 0,
                    .step = 0,
                    .humans = {},
                    .huds = {SnapshotHudEntry{.slot = 0, .removed = false, .changed = kAllHudFields, .words = words}}})
                .has_value());
    const auto same = receiver.apply(
        Snapshot{.sequence = 2,
                 .baseline = 1,
                 .step = 1,
                 .humans = {},
                 .huds = {SnapshotHudEntry{
                     .slot = 0, .removed = false, .changed = 1U << at(HudField::Rage), .words = partial}}});
    REQUIRE_FALSE(same.has_value());
    CHECK(same.error().code == ErrorCode::Invalid);
}
