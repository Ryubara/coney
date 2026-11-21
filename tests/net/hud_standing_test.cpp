// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD's standing state in the snapshots (docs/guides/snapshots.md#hud-standing): hud::HudStanding to its elements
// and texts and back, their deltas, removals, budget order and pinned bytes, the decoder's and the client's refusals,
// and the game's standing state as network play captures it. The session over the loopback is in sessions_test.cpp.
// Synthetic values; no game data.
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "hud/hud.h"
#include "net/bit_stream.h"
#include "net/game_humans.h"
#include "net/net_id.h"
#include "net/player_huds.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"
#include "net/snapshot.h"

using coney::ErrorCode;
using coney::hud::HudStanding;
using namespace coney::net;

namespace {

// Field `f`'s index into an element's words.
template <typename Field> constexpr std::size_t at(Field f) { return static_cast<std::size_t>(f); }

// A HUD in a mission: player 1's prompt, player 2's panel attached and flashing with his prompt cycling and his radar
// off and blue, two counter panels, two bars and the gauge, two scoreboard rows and the stopwatch, the HUD hidden.
HudStanding sample() {
    HudStanding standing;
    standing.players[0].prompt = "Press X";
    coney::hud::PlayerHudStanding& second = standing.players[1];
    second.panel = coney::hud::PanelFlags{
        .attached = true, .banner = 0x34, .shown = true, .mayShow = true, .forceShow = true, .flashFrames = 6};
    second.promptHidden = true;
    second.cycle = coney::hud::ActionCycle{.on = true, .framesPerIcon = 4, .blinkFrames = 6, .iconA = 79, .iconB = 73};
    second.radarOn = false;
    second.radarTint = coney::hud::RadarTint::Blue;
    coney::hud::SharedHudStanding& shared = standing.shared;
    shared.visible = false;
    shared.clubActionText = true;
    shared.radarScriptOn = false;
    shared.radarRest = 30.0F;
    shared.radarFast = 60.0F;
    shared.radarZoomScale = 1.5F;
    shared.counters[0] = coney::hud::CounterPanel{.used = true,
                                                  .kind = coney::hud::CounterKind::GotNeeded,
                                                  .label = "Tags",
                                                  .barWidth = 0.22F,
                                                  .value = 2.0F,
                                                  .target = 6.0F,
                                                  .visible = true,
                                                  .hideAtMs = {}};
    shared.counters[3] = coney::hud::CounterPanel{.used = true,
                                                  .kind = coney::hud::CounterKind::Bar,
                                                  .label = "Fuel",
                                                  .barWidth = 0.3F,
                                                  .value = 0.75F,
                                                  .target = 0.0F,
                                                  .visible = false,
                                                  .hideAtMs = {}};
    shared.bars[0] = coney::hud::GenericBar{.exists = true,
                                            .shown = true,
                                            .label = "Boss",
                                            .fill = 0.4F,
                                            .gradient = true,
                                            .colour = coney::graphics::kWhite,
                                            .width = 0.22F,
                                            .height = 0.025F};
    shared.bars[2] = coney::hud::GenericBar{.exists = true,
                                            .shown = false,
                                            .label = "",
                                            .fill = 1.0F,
                                            .gradient = false,
                                            .colour = coney::graphics::Rgba{10, 20, 30, 40},
                                            .width = 0.3F,
                                            .height = 0.05F};
    shared.gauge = coney::hud::ChaseGauge{
        .exists = true, .endIcon = 0x1a0004, .track = 0x1a0000, .value = 12.5F, .min = 0.0F, .max = 60.0F};
    shared.scores[0] = coney::hud::TextProgressRow{
        .active = true, .label = "Ajax", .score = 7, .colour = coney::graphics::Rgba{200, 20, 20, 255}};
    shared.scores[1] =
        coney::hud::TextProgressRow{.active = true, .label = "Cleon", .score = 0, .colour = coney::graphics::kWhite};
    shared.stopWatch = coney::hud::StopWatchDisplay{.shown = true, .label = "Time "};
    shared.stopWatchMs = 65000;
    return standing;
}

// The elements and texts of `standing` as a client holds them.
SnapshotHudElements elementsOf(const HudStanding& standing) {
    SnapshotHudElements held;
    for (const SnapshotHudElement& element : snapshotHudStanding(standing).elements) {
        held.emplace(element.key, element.words);
    }
    return held;
}
SnapshotHudTexts textsOf(const HudStanding& standing) {
    SnapshotHudTexts held;
    for (const SnapshotHudText& text : snapshotHudStanding(standing).texts) {
        held.emplace(text.key, text.text);
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

// Sends one snapshot of `standing` (and no humans or HUDs) from `sender` to `receiver`, which acknowledges it at once.
Snapshot exchange(SnapshotSender& sender, SnapshotReceiver& receiver, std::uint32_t step, const HudStanding& standing) {
    const SnapshotHudStanding parts = snapshotHudStanding(standing);
    const Snapshot snapshot = sender.build(step, {}, {}, parts.elements, parts.texts);
    const auto applied = receiver.apply(overTheWire(snapshot));
    REQUIRE(applied.has_value());
    REQUIRE(*applied == SnapshotApplied::Applied);
    REQUIRE(sender.acknowledge(snapshot.sequence).value());
    return snapshot;
}

// The keys of a snapshot's element or text entries, in order.
template <typename Entry> std::vector<std::uint8_t> keysOf(const std::vector<Entry>& entries) {
    std::vector<std::uint8_t> keys;
    keys.reserve(entries.size());
    for (const Entry& entry : entries) {
        keys.push_back(entry.key);
    }
    return keys;
}

// A synthetic human in full, standing still: what the budget test fills a snapshot with.
SnapshotHuman humanAt(std::uint16_t slot) {
    return SnapshotHuman{.id = NetId{.slot = slot, .generation = 1}, .words = {}, .priority = 1};
}

} // namespace

// Expected: unverified, #1524 (Coney's own field tables; the parts are hud.md's, snapshots.md#hud-standing)
TEST_CASE("HUD standing: the elements and texts round-trip, a number past its range held at its end", "[net][hud]") {
    const HudStanding standing = sample();
    const SnapshotHudStanding parts = snapshotHudStanding(standing);
    // Each player's element, the counter panels taken, the bars and the gauge that exist, the rows shown, the
    // stopwatch shown and the settings; a text for each that has one not empty (bar 2 has no label).
    CHECK(keysOf(parts.elements) ==
          std::vector<std::uint8_t>{0, 1, 4, 7, 9, 11, 13, 14, 15, kHudElementStopWatch, kHudElementSettings});
    CHECK(keysOf(parts.texts) == std::vector<std::uint8_t>{0, 4, 7, 9, 14, 15, kHudElementStopWatch});
    CHECK(parts.texts[0].text == "Press X");
    CHECK(parts.texts[2].text == "Fuel");
    CHECK(hudStanding(elementsOf(standing), textsOf(standing)) == standing);
    // The words are the field tables', field by field: an Exact field is the float's own bits.
    const HudElementWords& tags = parts.elements[2].words;
    CHECK(tags.at(at(HudCounterField::Value)) == std::bit_cast<std::uint32_t>(2.0F));
    CHECK(tags.at(at(HudCounterField::Kind)) == 2);
    CHECK(parts.elements[1].words.at(at(HudPlayerField::RadarTint)) == 1);
    CHECK(parts.elements[1].words.at(at(HudPlayerField::CycleIconA)) == 79);

    // Past the ranges: a flash count, a score and the stopwatch's time held; a text cut to 255 bytes; the cycle's
    // running part not carried.
    HudStanding wide = standing;
    wide.players[1].panel.flashFrames = 70000;
    wide.players[1].cycle.counter = 9;
    wide.players[1].cycle.second = true;
    wide.shared.scores[0].score = 1U << 25U;
    wide.shared.stopWatchMs = -5;
    wide.players[0].prompt = std::string(300, 'x');
    const HudStanding held = hudStanding(elementsOf(wide), textsOf(wide));
    CHECK(held.players[1].panel.flashFrames == 65535);
    CHECK(held.players[1].cycle == standing.players[1].cycle);
    CHECK(held.shared.scores[0].score == 16777215);
    CHECK(held.shared.stopWatchMs == 0);
    CHECK(held.players[0].prompt == std::string(kMaxHudTextBytes, 'x'));

    // Nothing to show: the players' elements and the settings alone, and no texts; read back as the HUD's defaults.
    const SnapshotHudStanding empty = snapshotHudStanding(HudStanding{});
    CHECK(keysOf(empty.elements) == std::vector<std::uint8_t>{0, 1, kHudElementSettings});
    CHECK(empty.texts.empty());
    CHECK(hudStanding({}, {}) == HudStanding{});
    // A player element past the HUD's two players is left out.
    SnapshotHudElements extra = elementsOf(standing);
    extra.emplace(std::uint8_t{3}, extra.at(1));
    CHECK(hudStanding(extra, textsOf(standing)) == standing);
}

// Expected: unverified, #1524 (Coney's own snapshots; the original has no network play)
TEST_CASE("HUD standing: the game's standing state is captured with the HUDs, and kept only while capture is on",
          "[net][hud]") {
    const HudStanding standing = sample();
    GameHumans game;
    game.captureHuds({}, &standing);
    const SnapshotHudStanding parts = snapshotHudStanding(standing);
    REQUIRE(game.hudElements().size() == parts.elements.size());
    CHECK(game.hudElements()[2].words == parts.elements[2].words);
    REQUIRE(game.hudTexts().size() == parts.texts.size());
    CHECK(game.hudTexts()[0].text == "Press X");
    game.captureHuds({});
    CHECK(game.hudElements().empty());
    game.captureHuds({}, &standing);
    game.clear();
    CHECK(game.hudElements().empty());
    CHECK(game.hudTexts().empty());
    game.setCapturing(false);
    game.captureHuds({}, &standing);
    CHECK(game.hudTexts().empty());
}

// Expected: unverified, #1524 (Coney's own snapshots; the original has no network play)
TEST_CASE("HUD standing: a snapshot carries the elements and texts in full first, then only what changed, and a part "
          "gone is removed",
          "[net][hud][snapshot]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    HudStanding standing = sample();

    // The first: every element with all its fields, every text.
    const Snapshot first = exchange(sender, receiver, 0, standing);
    REQUIRE(first.hudElements.size() == 11);
    for (const SnapshotHudElementEntry& entry : first.hudElements) {
        CHECK(entry.changed == allHudElementFields(entry.key));
    }
    CHECK(first.hudTexts.size() == 7);
    CHECK(receiver.hudElements() == elementsOf(standing));
    CHECK(receiver.hudTexts() == textsOf(standing));
    CHECK(*sender.holdingHudElements(first.sequence) == elementsOf(standing));
    CHECK(*sender.holdingHudTexts(first.sequence) == textsOf(standing));

    // Nothing changed: no entry.
    const Snapshot still = exchange(sender, receiver, 1, standing);
    CHECK(still.hudElements.empty());
    CHECK(still.hudTexts.empty());

    // A counter counts: its value alone, 5 + 1 + 5 changed bits + 32.
    standing.shared.counters[0].value = 3.0F;
    const Snapshot counted = exchange(sender, receiver, 2, standing);
    REQUIRE(counted.hudElements.size() == 1);
    CHECK(counted.hudElements[0].key == kHudElementCounters);
    CHECK(counted.hudElements[0].changed == 1U << at(HudCounterField::Value));
    CHECK(snapshotHudElementEntryBits(counted.hudElements[0]) == 5 + 1 + 5 + 32);
    CHECK(counted.hudTexts.empty());

    // A prompt's text changes: that text alone, 5 + 1 + a length byte + its bytes.
    standing.players[0].prompt = "Press O";
    const Snapshot prompted = exchange(sender, receiver, 3, standing);
    CHECK(prompted.hudElements.empty());
    REQUIRE(prompted.hudTexts.size() == 1);
    CHECK(prompted.hudTexts[0].key == 0);
    CHECK(prompted.hudTexts[0].text == "Press O");
    CHECK(snapshotHudTextEntryBits(prompted.hudTexts[0]) == 5 + 1 + 8 + (7 * 8));
    CHECK(hudStanding(receiver.hudElements(), receiver.hudTexts()) == standing);

    // A counter released and the prompt cleared: the element and both texts removed.
    standing.shared.counters[0] = coney::hud::CounterPanel{};
    standing.players[0].prompt.clear();
    const Snapshot released = exchange(sender, receiver, 4, standing);
    REQUIRE(released.hudElements.size() == 1);
    CHECK(released.hudElements[0].removed);
    CHECK(released.hudElements[0].key == kHudElementCounters);
    REQUIRE(released.hudTexts.size() == 2);
    CHECK(released.hudTexts[0].key == 0);
    CHECK(released.hudTexts[0].removed);
    CHECK(released.hudTexts[1].key == kHudElementCounters);
    CHECK(released.hudTexts[1].removed);
    CHECK(hudStanding(receiver.hudElements(), receiver.hudTexts()) == standing);

    // A lost acknowledgement: the next delta goes against the older baseline and still leaves the right state.
    standing.shared.bars[0].fill = 0.3F;
    const Snapshot unacknowledged =
        sender.build(5, {}, {}, snapshotHudStanding(standing).elements, snapshotHudStanding(standing).texts);
    REQUIRE(receiver.apply(overTheWire(unacknowledged)).has_value());
    standing.shared.scores[1].label = "Vermin";
    const SnapshotHudStanding parts = snapshotHudStanding(standing);
    const Snapshot next = sender.build(6, {}, {}, parts.elements, parts.texts);
    CHECK(next.baseline == released.sequence);
    REQUIRE(next.hudElements.size() == 1);
    REQUIRE(next.hudTexts.size() == 1);
    REQUIRE(receiver.apply(overTheWire(next)).has_value());
    CHECK(hudStanding(receiver.hudElements(), receiver.hudTexts()) == standing);
}

// Expected: unverified, #1524 (Coney's own snapshot packet; the original has no network play)
TEST_CASE("HUD standing: the elements' and texts' bytes are pinned, round-trip and refuse what breaks their rules",
          "[net][hud][snapshot]") {
    HudElementWords two{};
    two.at(at(HudCounterField::Value)) = std::bit_cast<std::uint32_t>(2.0F);
    REQUIRE(two.at(at(HudCounterField::Value)) == 0x40000000);
    const Snapshot small{
        .sequence = 3,
        .baseline = 2,
        .step = 7,
        .humans = {},
        .huds = {},
        .hudElements = {SnapshotHudElementEntry{
            .key = kHudElementCounters, .removed = false, .changed = 1U << at(HudCounterField::Value), .words = two}},
        .hudTexts = {SnapshotHudTextEntry{.key = 0, .removed = false, .text = "Go"}}};
    // Id 10; sequence 3; baseline 1 back; step 7; no humans; no HUDs; one element. From its first bit: key 4 (5 bits),
    // removed 0, fields 0 and 1 unchanged = 0x04; field 2 changed and its word's low 7 bits = 0x01; bits 7 to 22 =
    // 0x00 0x00; bits 23 to 30 = 0x80 (bit 30 set); bit 31, fields 3 and 4 unchanged, then one text (a varint 1 from
    // bit 3) = 0x08; the varint's top bits and key 0 = 0x00; removed 0 and the length 2 from bit 1 = 0x04; then "Go"
    // from bit 1 = 0x8e 0xde 0x00.
    const std::vector<std::byte> expected{
        std::byte{0x0a}, std::byte{0x03}, std::byte{0x01}, std::byte{0x07}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x01}, std::byte{0x04}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80},
        std::byte{0x08}, std::byte{0x00}, std::byte{0x04}, std::byte{0x8e}, std::byte{0xde}, std::byte{0x00}};
    CHECK(encode(small) == expected);
    CHECK(overTheWire(small) == small);
    const std::size_t bits = snapshotHeaderBits(small, 0, 0, 1, 1) + snapshotHudElementEntryBits(small.hudElements[0]) +
                             snapshotHudTextEntryBits(small.hudTexts[0]);
    CHECK((bits + 7) / 8 == expected.size());

    const auto refused = [](const std::vector<std::byte>& packet) {
        const auto decoded = decode(packet);
        REQUIRE_FALSE(decoded.has_value());
        CHECK(decoded.error().code == ErrorCode::Invalid);
    };
    // The header of a snapshot with no humans and no HUDs, then what `tail` writes: the elements and the texts.
    const auto packet = [](const std::function<void(BitWriter&)>& tail) {
        BitWriter out;
        for (const std::uint32_t b : {0x0aU, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U}) {
            out.writeBits(b, 8);
        }
        tail(out);
        return std::vector<std::byte>(out.bytes().begin(), out.bytes().end());
    };
    // Counter `key` with its value changed to `word`, or nothing at all.
    const auto counter = [](BitWriter& out, std::uint32_t key, bool changes, std::uint32_t word = 0x40000000) {
        out.writeBits(key, kHudElementBits);
        out.writeBool(false);
        for (std::size_t field = 0; field < static_cast<std::size_t>(HudCounterField::Count); ++field) {
            const bool changed = changes && field == at(HudCounterField::Value);
            out.writeBool(changed);
            if (changed) {
                out.writeBits(word, 32);
            }
        }
    };
    // A text of key `key`.
    const auto text = [](BitWriter& out, std::uint32_t key, std::string_view value) {
        out.writeBits(key, kHudElementBits);
        out.writeBool(false);
        out.writeVarUint(value.size());
        for (const char c : value) {
            out.writeBits(static_cast<std::uint8_t>(c), 8);
        }
    };
    // A clean element and text decode.
    CHECK(decode(packet([&](BitWriter& out) {
              out.writeVarUint(1);
              counter(out, 4, true);
              out.writeVarUint(1);
              text(out, 4, "Tags");
          })).has_value());
    // More elements than there are keys.
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(kHudElements + 1);
        counter(out, 4, true);
    }));
    // A key past the last.
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(1);
        counter(out, kHudElements, true);
    }));
    // An element with nothing changed.
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(1);
        counter(out, 4, false);
        out.writeVarUint(0);
    }));
    // Keys not rising.
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(2);
        counter(out, 5, true);
        counter(out, 4, true);
        out.writeVarUint(0);
    }));
    // An Exact word that is not a finite number (NaN).
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(1);
        counter(out, 4, true, 0x7fc00000);
        out.writeVarUint(0);
    }));
    // A text for an element with none (the gauge), an empty text, texts not rising, and more texts than keys.
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(0);
        out.writeVarUint(1);
        text(out, kHudElementGauge, "x");
    }));
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(0);
        out.writeVarUint(1);
        text(out, 4, "");
    }));
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(0);
        out.writeVarUint(2);
        text(out, 5, "a");
        text(out, 5, "b");
    }));
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(0);
        out.writeVarUint(kHudElements + 1);
        text(out, 4, "a");
    }));
    // A text over its limit.
    refused(packet([&](BitWriter& out) {
        out.writeVarUint(0);
        out.writeVarUint(1);
        text(out, 4, std::string(kMaxHudTextBytes + 1, 'x'));
    }));
}

// Expected: unverified, #1524 (Coney's own snapshots; the original has no network play)
TEST_CASE("HUD standing: the client refuses an element or text removal or change its baseline does not allow",
          "[net][hud][snapshot]") {
    const auto snapshotOf = [](std::uint32_t sequence, std::vector<SnapshotHudElementEntry> elements,
                               std::vector<SnapshotHudTextEntry> texts) {
        return Snapshot{.sequence = sequence,
                        .baseline = sequence - 1,
                        .step = sequence,
                        .humans = {},
                        .huds = {},
                        .hudElements = std::move(elements),
                        .hudTexts = std::move(texts)};
    };
    const auto invalid = [](const std::expected<SnapshotApplied, coney::Error>& applied) {
        REQUIRE_FALSE(applied.has_value());
        CHECK(applied.error().code == ErrorCode::Invalid);
    };
    SnapshotReceiver receiver;
    // A removal of an element or a text the baseline does not hold.
    invalid(receiver.apply(snapshotOf(1, {SnapshotHudElementEntry{.key = 4, .removed = true}}, {})));
    invalid(receiver.apply(snapshotOf(1, {}, {SnapshotHudTextEntry{.key = 4, .removed = true}})));
    // A new element with fields left out.
    HudElementWords words{};
    words.at(at(HudCounterField::Value)) = std::bit_cast<std::uint32_t>(2.0F);
    invalid(receiver.apply(snapshotOf(
        1, {SnapshotHudElementEntry{.key = 4, .changed = 1U << at(HudCounterField::Value), .words = words}}, {})));
    // Held, then a field "changed" to the word it has, and a text "changed" to the text it has.
    REQUIRE(
        receiver
            .apply(snapshotOf(1, {SnapshotHudElementEntry{.key = 4, .changed = allHudElementFields(4), .words = words}},
                              {SnapshotHudTextEntry{.key = 4, .text = "Tags"}}))
            .has_value());
    invalid(receiver.apply(snapshotOf(
        2, {SnapshotHudElementEntry{.key = 4, .changed = 1U << at(HudCounterField::Value), .words = words}}, {})));
    invalid(receiver.apply(snapshotOf(2, {}, {SnapshotHudTextEntry{.key = 4, .text = "Tags"}})));
}

// Expected: unverified, #1524 (Coney's own budget order; snapshots.md#budget)
TEST_CASE("HUD standing: the elements go with the HUDs and the texts after them, before the humans' changes",
          "[net][hud][snapshot]") {
    // Sixty humans in full would fill the first snapshot on their own; the HUD's standing state comes before them.
    SnapshotSender sender;
    SnapshotReceiver receiver;
    std::vector<SnapshotHuman> humans;
    for (std::uint16_t slot = 1; slot <= 60; ++slot) {
        humans.push_back(humanAt(slot));
    }
    const std::vector<SnapshotHud> huds{SnapshotHud{.slot = 0, .words = {}}, SnapshotHud{.slot = 1, .words = {}}};
    const SnapshotHudStanding parts = snapshotHudStanding(sample());
    const Snapshot snapshot = sender.build(0, humans, huds, parts.elements, parts.texts);
    REQUIRE(receiver.apply(overTheWire(snapshot)).has_value());
    CHECK(snapshot.huds.size() == 2);
    CHECK(snapshot.hudElements.size() == parts.elements.size());
    CHECK(snapshot.hudTexts.size() == parts.texts.size());
    CHECK(hudStanding(receiver.hudElements(), receiver.hudTexts()) == sample());
    // The humans have what is left of 9,600 bits, each 302 in full.
    std::size_t used = snapshotHeaderBits(snapshot, humans.size(), 2, parts.elements.size(), parts.texts.size());
    for (const SnapshotHudEntry& entry : snapshot.huds) {
        used += snapshotHudEntryBits(entry);
    }
    for (const SnapshotHudElementEntry& entry : snapshot.hudElements) {
        used += snapshotHudElementEntryBits(entry);
    }
    for (const SnapshotHudTextEntry& entry : snapshot.hudTexts) {
        used += snapshotHudTextEntryBits(entry);
    }
    CHECK(snapshot.humans.size() == (9600 - used) / 302);
    CHECK(encode(snapshot).size() <= kSnapshotBudgetBytes);
}
