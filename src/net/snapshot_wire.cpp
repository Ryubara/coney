// SPDX-License-Identifier: GPL-3.0-or-later
// The Snapshot message's body on the wire (docs/guides/snapshots.md#packet), apart from protocol.cpp: its rules, its
// entries' changed bits and words, and the bits the server's budget counts.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>

#include "core/assert.h"
#include "core/error.h"
#include "net/bit_stream.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/protocol_wire.h"
#include "net/replicated_fields.h"

namespace coney::net {

namespace {

// Bits writeVarUint() takes for `value`: 8 for each 7 bits it needs, 8 at least.
std::size_t varUintBits(std::uint64_t value) {
    std::size_t groups = 1;
    while (value >= 0x80U) {
        value >>= 7U;
        ++groups;
    }
    return groups * 8;
}

// Whether a Snapshot's HUD entries keep their rules: null when they do, else the reason.
const char* hudRuleBroken(const Snapshot& snapshot) {
    if (snapshot.huds.size() > kMaxPlayerSlots) {
        return "network snapshot carries more HUDs than there are player slots";
    }
    for (std::size_t i = 0; i < snapshot.huds.size(); ++i) {
        const SnapshotHudEntry& entry = snapshot.huds[i];
        if (entry.slot >= kMaxPlayerSlots) {
            return "network snapshot names a player slot past the last";
        }
        if (i > 0 && snapshot.huds[i - 1].slot >= entry.slot) {
            return "network snapshot's HUD slots are not in rising order";
        }
        if (entry.removed ? entry.changed != 0 : (entry.changed == 0 || (entry.changed & ~kAllHudFields) != 0)) {
            return "network snapshot HUD entry changes nothing, or a removal changes something";
        }
        for (std::size_t field = 0; field < kHudFieldCount; ++field) {
            if ((entry.changed & (1U << field)) == 0 && entry.words.at(field) != 0) {
                return "network snapshot HUD entry has a word for a field it does not change";
            }
        }
    }
    return nullptr;
}

// Whether a Snapshot's HUD elements and texts keep their rules: null when they do, else the reason.
const char* hudStandingRuleBroken(const Snapshot& snapshot) {
    if (snapshot.hudElements.size() > kHudElements || snapshot.hudTexts.size() > kHudElements) {
        return "network snapshot carries more HUD elements or texts than there are keys";
    }
    for (std::size_t i = 0; i < snapshot.hudElements.size(); ++i) {
        const SnapshotHudElementEntry& entry = snapshot.hudElements[i];
        if (entry.key >= kHudElements) {
            return "network snapshot names a HUD element past the last";
        }
        if (i > 0 && snapshot.hudElements[i - 1].key >= entry.key) {
            return "network snapshot's HUD elements are not in rising order";
        }
        const std::uint32_t all = allHudElementFields(entry.key);
        if (entry.removed ? entry.changed != 0 : (entry.changed == 0 || (entry.changed & ~all) != 0)) {
            return "network snapshot HUD element changes nothing, or a removal changes something";
        }
        for (std::size_t field = 0; field < kMaxHudElementFields; ++field) {
            if ((entry.changed & (1U << field)) == 0 && entry.words.at(field) != 0) {
                return "network snapshot HUD element has a word for a field it does not change";
            }
        }
    }
    for (std::size_t i = 0; i < snapshot.hudTexts.size(); ++i) {
        const SnapshotHudTextEntry& entry = snapshot.hudTexts[i];
        if (!hudElementHasText(entry.key)) {
            return "network snapshot names a HUD text no element has";
        }
        if (i > 0 && snapshot.hudTexts[i - 1].key >= entry.key) {
            return "network snapshot's HUD texts are not in rising order";
        }
        if (entry.removed ? !entry.text.empty() : (entry.text.empty() || entry.text.size() > kMaxHudTextBytes)) {
            return "network snapshot HUD text is empty or too long, or a removal has a text";
        }
    }
    return nullptr;
}

// Whether a Snapshot's radar blips keep their rules: null when they do, else the reason.
const char* blipRuleBroken(const Snapshot& snapshot) {
    if (snapshot.blips.size() > kMaxRadarBlips) {
        return "network snapshot carries more radar blips than there are slots";
    }
    for (std::size_t i = 0; i < snapshot.blips.size(); ++i) {
        const SnapshotBlipEntry& entry = snapshot.blips[i];
        if (entry.slot >= kMaxRadarBlips) {
            return "network snapshot names a radar blip slot past the last";
        }
        if (i > 0 && snapshot.blips[i - 1].slot >= entry.slot) {
            return "network snapshot's radar blip slots are not in rising order";
        }
        if (entry.removed ? entry.changed != 0 : (entry.changed == 0 || (entry.changed & ~kAllBlipFields) != 0)) {
            return "network snapshot radar blip changes nothing, or a removal changes something";
        }
        for (std::size_t field = 0; field < kBlipFieldCount; ++field) {
            if ((entry.changed & (1U << field)) == 0 && entry.words.at(field) != 0) {
                return "network snapshot radar blip has a word for a field it does not change";
            }
        }
    }
    return nullptr;
}

// Whether a Snapshot keeps its rules (encode() asserts them, decode() refuses what breaks them): null when it does,
// else the reason.
const char* snapshotRuleBroken(const Snapshot& snapshot) {
    if (snapshot.sequence == 0) {
        return "network snapshot has sequence 0";
    }
    if (snapshot.baseline != 0 &&
        (snapshot.baseline >= snapshot.sequence || snapshot.sequence - snapshot.baseline > kSnapshotHistory)) {
        return "network snapshot's baseline is not among the snapshots before it";
    }
    if (const char* broken = hudRuleBroken(snapshot)) {
        return broken;
    }
    if (const char* broken = hudStandingRuleBroken(snapshot)) {
        return broken;
    }
    if (const char* broken = blipRuleBroken(snapshot)) {
        return broken;
    }
    for (std::size_t i = 0; i < snapshot.humans.size(); ++i) {
        const SnapshotEntry& entry = snapshot.humans[i];
        if (!entry.id.valid()) {
            return "network snapshot names no id";
        }
        if (i > 0 && !(snapshot.humans[i - 1].id < entry.id)) {
            return "network snapshot's ids are not in rising order";
        }
        if (entry.removed ? entry.changed != 0 : (entry.changed == 0 || (entry.changed & ~kAllHumanFields) != 0)) {
            return "network snapshot entry changes nothing, or a removal changes something";
        }
        for (std::size_t field = 0; field < kHumanFieldCount; ++field) {
            if ((entry.changed & (1U << field)) == 0 && entry.words.at(field) != 0) {
                return "network snapshot entry has a word for a field it does not change";
            }
        }
    }
    return nullptr;
}

// Writes a Snapshot entry as Snapshot's comment lays it out.
void writeSnapshotEntry(BitWriter& out, const SnapshotEntry& entry) {
    const auto fields = fieldTable(ReplicatedKind::Human);
    writeNetId(out, entry.id);
    out.writeBool(entry.removed);
    if (entry.removed) {
        return;
    }
    for (std::size_t field = 0; field < kHumanFieldCount; ++field) {
        const bool changed = (entry.changed & (1U << field)) != 0;
        out.writeBool(changed);
        if (changed) {
            out.writeBits(entry.words.at(field), fields[field].bits);
        }
    }
}

// Writes a Snapshot HUD entry as Snapshot's comment lays it out.
void writeSnapshotHudEntry(BitWriter& out, const SnapshotHudEntry& entry) {
    const auto fields = fieldTable(ReplicatedKind::PlayerHud);
    out.writeBits(entry.slot, kPlayerSlotBits);
    out.writeBool(entry.removed);
    if (entry.removed) {
        return;
    }
    for (std::size_t field = 0; field < kHudFieldCount; ++field) {
        const bool changed = (entry.changed & (1U << field)) != 0;
        out.writeBool(changed);
        if (changed) {
            out.writeBits(entry.words.at(field), fields[field].bits);
        }
    }
}

// Reads a Snapshot HUD entry written by writeSnapshotHudEntry(); a word over its field's range is Invalid.
std::expected<SnapshotHudEntry, Error> readSnapshotHudEntry(BitReader& in) {
    const auto fields = fieldTable(ReplicatedKind::PlayerHud);
    SnapshotHudEntry entry;
    auto slot = in.readBits(kPlayerSlotBits);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    entry.slot = static_cast<std::uint8_t>(*slot);
    auto removed = in.readBool();
    if (!removed) {
        return std::unexpected(removed.error());
    }
    entry.removed = *removed;
    if (entry.removed) {
        return entry;
    }
    for (std::size_t field = 0; field < kHudFieldCount; ++field) {
        auto changed = in.readBool();
        if (!changed) {
            return std::unexpected(changed.error());
        }
        if (!*changed) {
            continue;
        }
        auto word = in.readBits(fields[field].bits);
        if (!word) {
            return std::unexpected(word.error());
        }
        if (auto value = fieldValue(fields[field], *word); !value) {
            return std::unexpected(value.error());
        }
        entry.changed |= 1U << field;
        entry.words.at(field) = *word;
    }
    return entry;
}

// Writes a Snapshot radar blip as Snapshot's comment lays it out.
void writeSnapshotBlip(BitWriter& out, const SnapshotBlipEntry& entry) {
    const auto fields = fieldTable(ReplicatedKind::HudBlip);
    out.writeBits(entry.slot, kRadarBlipSlotBits);
    out.writeBool(entry.removed);
    if (entry.removed) {
        return;
    }
    for (std::size_t field = 0; field < kBlipFieldCount; ++field) {
        const bool changed = (entry.changed & (1U << field)) != 0;
        out.writeBool(changed);
        if (changed) {
            out.writeBits(entry.words.at(field), fields[field].bits);
        }
    }
}

// Reads a Snapshot radar blip written by writeSnapshotBlip(); a word over its field's range is Invalid.
std::expected<SnapshotBlipEntry, Error> readSnapshotBlip(BitReader& in) {
    const auto fields = fieldTable(ReplicatedKind::HudBlip);
    SnapshotBlipEntry entry;
    auto slot = in.readBits(kRadarBlipSlotBits);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    entry.slot = static_cast<std::uint8_t>(*slot);
    auto removed = in.readBool();
    if (!removed) {
        return std::unexpected(removed.error());
    }
    entry.removed = *removed;
    if (entry.removed) {
        return entry;
    }
    for (std::size_t field = 0; field < kBlipFieldCount; ++field) {
        auto changed = in.readBool();
        if (!changed) {
            return std::unexpected(changed.error());
        }
        if (!*changed) {
            continue;
        }
        auto word = in.readBits(fields[field].bits);
        if (!word) {
            return std::unexpected(word.error());
        }
        if (auto value = fieldValue(fields[field], *word); !value) {
            return std::unexpected(value.error());
        }
        entry.changed |= 1U << field;
        entry.words.at(field) = *word;
    }
    return entry;
}

// Writes a Snapshot HUD element as Snapshot's comment lays it out.
void writeSnapshotHudElement(BitWriter& out, const SnapshotHudElementEntry& entry) {
    out.writeBits(entry.key, kHudElementBits);
    out.writeBool(entry.removed);
    if (entry.removed) {
        return;
    }
    const auto fields = fieldTable(hudElementKind(entry.key));
    for (std::size_t field = 0; field < fields.size(); ++field) {
        const bool changed = (entry.changed & (1U << field)) != 0;
        out.writeBool(changed);
        if (changed) {
            out.writeBits(entry.words.at(field), fields[field].bits);
        }
    }
}

// Reads a Snapshot HUD element written by writeSnapshotHudElement(); a key past the last, or a word over its field's
// range, is Invalid.
std::expected<SnapshotHudElementEntry, Error> readSnapshotHudElement(BitReader& in) {
    SnapshotHudElementEntry entry;
    auto key = in.readBits(kHudElementBits);
    if (!key) {
        return std::unexpected(key.error());
    }
    if (*key >= kHudElements) {
        return fail(ErrorCode::Invalid, "network snapshot names a HUD element past the last");
    }
    entry.key = static_cast<std::uint8_t>(*key);
    auto removed = in.readBool();
    if (!removed) {
        return std::unexpected(removed.error());
    }
    entry.removed = *removed;
    if (entry.removed) {
        return entry;
    }
    const auto fields = fieldTable(hudElementKind(entry.key));
    for (std::size_t field = 0; field < fields.size(); ++field) {
        auto changed = in.readBool();
        if (!changed) {
            return std::unexpected(changed.error());
        }
        if (!*changed) {
            continue;
        }
        auto word = in.readBits(fields[field].bits);
        if (!word) {
            return std::unexpected(word.error());
        }
        if (auto value = fieldValue(fields[field], *word); !value) {
            return std::unexpected(value.error());
        }
        entry.changed |= 1U << field;
        entry.words.at(field) = *word;
    }
    return entry;
}

// Writes a Snapshot HUD text as Snapshot's comment lays it out.
void writeSnapshotHudText(BitWriter& out, const SnapshotHudTextEntry& entry) {
    out.writeBits(entry.key, kHudElementBits);
    out.writeBool(entry.removed);
    if (!entry.removed) {
        out.writeString(entry.text, kMaxHudTextBytes);
    }
}

// Reads a Snapshot HUD text written by writeSnapshotHudText(); a text over kMaxHudTextBytes is Invalid (the rules
// check the rest).
std::expected<SnapshotHudTextEntry, Error> readSnapshotHudText(BitReader& in) {
    SnapshotHudTextEntry entry;
    auto key = in.readBits(kHudElementBits);
    if (!key) {
        return std::unexpected(key.error());
    }
    entry.key = static_cast<std::uint8_t>(*key);
    auto removed = in.readBool();
    if (!removed) {
        return std::unexpected(removed.error());
    }
    entry.removed = *removed;
    if (entry.removed) {
        return entry;
    }
    auto text = in.readString(kMaxHudTextBytes);
    if (!text) {
        return std::unexpected(text.error());
    }
    entry.text = std::move(*text);
    return entry;
}

// Reads a Snapshot entry written by writeSnapshotEntry(); a word over its field's range is Invalid.
std::expected<SnapshotEntry, Error> readSnapshotEntry(BitReader& in) {
    const auto fields = fieldTable(ReplicatedKind::Human);
    SnapshotEntry entry;
    auto id = readNetId(in);
    if (!id) {
        return std::unexpected(id.error());
    }
    entry.id = *id;
    auto removed = in.readBool();
    if (!removed) {
        return std::unexpected(removed.error());
    }
    entry.removed = *removed;
    if (entry.removed) {
        return entry;
    }
    for (std::size_t field = 0; field < kHumanFieldCount; ++field) {
        auto changed = in.readBool();
        if (!changed) {
            return std::unexpected(changed.error());
        }
        if (!*changed) {
            continue;
        }
        auto word = in.readBits(fields[field].bits);
        if (!word) {
            return std::unexpected(word.error());
        }
        if (auto value = fieldValue(fields[field], *word); !value) {
            return std::unexpected(value.error());
        }
        entry.changed |= 1U << field;
        entry.words.at(field) = *word;
    }
    return entry;
}

} // namespace

void writeSnapshotBody(BitWriter& out, const Snapshot& snapshot) {
    CONEY_ASSERT(snapshotRuleBroken(snapshot) == nullptr);
    out.writeVarUint(snapshot.sequence);
    out.writeVarUint(snapshot.baseline == 0 ? 0 : snapshot.sequence - snapshot.baseline);
    out.writeVarUint(snapshot.step);
    out.writeVarUint(snapshot.humans.size());
    for (const SnapshotEntry& entry : snapshot.humans) {
        writeSnapshotEntry(out, entry);
    }
    out.writeVarUint(snapshot.huds.size());
    for (const SnapshotHudEntry& entry : snapshot.huds) {
        writeSnapshotHudEntry(out, entry);
    }
    out.writeVarUint(snapshot.hudElements.size());
    for (const SnapshotHudElementEntry& entry : snapshot.hudElements) {
        writeSnapshotHudElement(out, entry);
    }
    out.writeVarUint(snapshot.hudTexts.size());
    for (const SnapshotHudTextEntry& entry : snapshot.hudTexts) {
        writeSnapshotHudText(out, entry);
    }
    out.writeVarUint(snapshot.blips.size());
    for (const SnapshotBlipEntry& entry : snapshot.blips) {
        writeSnapshotBlip(out, entry);
    }
}

std::expected<Snapshot, Error> readSnapshotBody(BitReader& in) {
    Snapshot snapshot;
    auto sequence = readSequence(in);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    snapshot.sequence = *sequence;
    auto back = readSequence(in);
    if (!back) {
        return std::unexpected(back.error());
    }
    if (*back > kSnapshotHistory || (*back != 0 && *back >= snapshot.sequence)) {
        return fail(ErrorCode::Invalid, "network snapshot's baseline is not among the snapshots before it");
    }
    snapshot.baseline = *back == 0 ? 0 : snapshot.sequence - *back;
    auto step = readSequence(in);
    if (!step) {
        return std::unexpected(step.error());
    }
    snapshot.step = *step;
    auto count = in.readVarUint();
    if (!count) {
        return std::unexpected(count.error());
    }
    // Each entry takes an id and a bit at least: a count the packet cannot hold is refused before allocating.
    if (*count > in.remainingBits() / (kNetIdBits + 1)) {
        return fail(ErrorCode::Invalid, "network snapshot counts more entries than its packet holds");
    }
    snapshot.humans.reserve(static_cast<std::size_t>(*count));
    for (std::uint64_t i = 0; i < *count; ++i) {
        auto entry = readSnapshotEntry(in);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        snapshot.humans.push_back(*entry);
    }
    // The humans' rules before the HUDs are read, so a broken entry is refused as Invalid wherever the packet ends.
    if (const char* broken = snapshotRuleBroken(snapshot)) {
        return fail(ErrorCode::Invalid, broken);
    }
    auto hudCount = in.readVarUint();
    if (!hudCount) {
        return std::unexpected(hudCount.error());
    }
    if (*hudCount > kMaxPlayerSlots) {
        return fail(ErrorCode::Invalid, "network snapshot carries more HUDs than there are player slots");
    }
    for (std::uint64_t i = 0; i < *hudCount; ++i) {
        auto entry = readSnapshotHudEntry(in);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        snapshot.huds.push_back(*entry);
    }
    if (const char* broken = hudRuleBroken(snapshot)) {
        return fail(ErrorCode::Invalid, broken);
    }
    // The HUD's standing state: its elements, then its texts, each count checked before any is read.
    auto elementCount = in.readVarUint();
    if (!elementCount) {
        return std::unexpected(elementCount.error());
    }
    if (*elementCount > kHudElements) {
        return fail(ErrorCode::Invalid, "network snapshot carries more HUD elements or texts than there are keys");
    }
    for (std::uint64_t i = 0; i < *elementCount; ++i) {
        auto entry = readSnapshotHudElement(in);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        snapshot.hudElements.push_back(*entry);
    }
    if (const char* broken = hudStandingRuleBroken(snapshot)) {
        return fail(ErrorCode::Invalid, broken);
    }
    auto textCount = in.readVarUint();
    if (!textCount) {
        return std::unexpected(textCount.error());
    }
    if (*textCount > kHudElements) {
        return fail(ErrorCode::Invalid, "network snapshot carries more HUD elements or texts than there are keys");
    }
    for (std::uint64_t i = 0; i < *textCount; ++i) {
        auto entry = readSnapshotHudText(in);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        snapshot.hudTexts.push_back(std::move(*entry));
    }
    if (const char* broken = hudStandingRuleBroken(snapshot)) {
        return fail(ErrorCode::Invalid, broken);
    }
    // The radar's blips: their count checked against the slots and against what the packet can hold before any is read.
    auto blipCount = in.readVarUint();
    if (!blipCount) {
        return std::unexpected(blipCount.error());
    }
    if (*blipCount > kMaxRadarBlips) {
        return fail(ErrorCode::Invalid, "network snapshot carries more radar blips than there are slots");
    }
    if (*blipCount > in.remainingBits() / (kRadarBlipSlotBits + 1)) {
        return fail(ErrorCode::Invalid, "network snapshot counts more radar blips than its packet holds");
    }
    for (std::uint64_t i = 0; i < *blipCount; ++i) {
        auto entry = readSnapshotBlip(in);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        snapshot.blips.push_back(*entry);
    }
    if (const char* broken = blipRuleBroken(snapshot)) {
        return fail(ErrorCode::Invalid, broken);
    }
    return snapshot;
}

std::size_t snapshotEntryBits(const SnapshotEntry& entry) {
    std::size_t bits = kNetIdBits + 1;
    if (entry.removed) {
        return bits;
    }
    const auto fields = fieldTable(ReplicatedKind::Human);
    for (std::size_t field = 0; field < kHumanFieldCount; ++field) {
        bits += 1;
        if ((entry.changed & (1U << field)) != 0) {
            bits += static_cast<std::size_t>(fields[field].bits);
        }
    }
    return bits;
}

std::size_t snapshotHudEntryBits(const SnapshotHudEntry& entry) {
    std::size_t bits = kPlayerSlotBits + 1;
    if (entry.removed) {
        return bits;
    }
    const auto fields = fieldTable(ReplicatedKind::PlayerHud);
    for (std::size_t field = 0; field < kHudFieldCount; ++field) {
        bits += 1;
        if ((entry.changed & (1U << field)) != 0) {
            bits += static_cast<std::size_t>(fields[field].bits);
        }
    }
    return bits;
}

ReplicatedKind hudElementKind(std::uint8_t key) {
    CONEY_ASSERT(key < kHudElements);
    if (key < kHudElementCounters) {
        return ReplicatedKind::HudPlayer;
    }
    if (key < kHudElementBars) {
        return ReplicatedKind::HudCounter;
    }
    if (key < kHudElementGauge) {
        return ReplicatedKind::HudBar;
    }
    if (key == kHudElementGauge) {
        return ReplicatedKind::HudGauge;
    }
    if (key < kHudElementStopWatch) {
        return ReplicatedKind::HudScoreRow;
    }
    return key == kHudElementStopWatch ? ReplicatedKind::HudStopWatch : ReplicatedKind::HudSettings;
}

std::size_t hudElementFieldCount(std::uint8_t key) { return fieldTable(hudElementKind(key)).size(); }

std::uint32_t allHudElementFields(std::uint8_t key) { return (1U << hudElementFieldCount(key)) - 1U; }

bool hudElementHasText(std::uint8_t key) {
    return key < kHudElements && key != kHudElementGauge && key != kHudElementSettings;
}

std::size_t snapshotHudElementEntryBits(const SnapshotHudElementEntry& entry) {
    std::size_t bits = kHudElementBits + 1;
    if (entry.removed) {
        return bits;
    }
    const auto fields = fieldTable(hudElementKind(entry.key));
    for (std::size_t field = 0; field < fields.size(); ++field) {
        bits += 1;
        if ((entry.changed & (1U << field)) != 0) {
            bits += static_cast<std::size_t>(fields[field].bits);
        }
    }
    return bits;
}

std::size_t snapshotHudTextEntryBits(const SnapshotHudTextEntry& entry) {
    const std::size_t bits = kHudElementBits + 1;
    return entry.removed ? bits : bits + varUintBits(entry.text.size()) + (entry.text.size() * 8);
}

std::size_t snapshotBlipEntryBits(const SnapshotBlipEntry& entry) {
    std::size_t bits = kRadarBlipSlotBits + 1;
    if (entry.removed) {
        return bits;
    }
    const auto fields = fieldTable(ReplicatedKind::HudBlip);
    for (std::size_t field = 0; field < kBlipFieldCount; ++field) {
        bits += 1;
        if ((entry.changed & (1U << field)) != 0) {
            bits += static_cast<std::size_t>(fields[field].bits);
        }
    }
    return bits;
}

std::uint32_t blipNoPlaceWord() {
    return fieldWord(fieldTable(ReplicatedKind::HudBlip)[static_cast<std::size_t>(HudBlipField::PlaceX)],
                     FieldValue{0.0F});
}

const char* blipWordsBroken(const BlipWords& words) {
    const std::uint32_t follows = words.at(static_cast<std::size_t>(HudBlipField::Follows));
    const bool hasPlace = words.at(static_cast<std::size_t>(HudBlipField::HasPlace)) != 0;
    // A word of 0 follows nobody; any other is an id, whose generation (the bits above the slot's) is not 0.
    if (follows != 0 && !netIdFromNumber(follows).valid()) {
        return "network snapshot's radar blip follows a number that is no network id";
    }
    if (follows != 0 && hasPlace) {
        return "network snapshot's radar blip follows a human and also has a place";
    }
    if (!hasPlace) {
        for (const HudBlipField coordinate : {HudBlipField::PlaceX, HudBlipField::PlaceY, HudBlipField::PlaceZ}) {
            if (words.at(static_cast<std::size_t>(coordinate)) != blipNoPlaceWord()) {
                return "network snapshot's radar blip has no place but a coordinate of one";
            }
        }
    }
    return nullptr;
}

std::size_t snapshotHeaderBits(const Snapshot& snapshot, std::size_t entries, std::size_t hudEntries,
                               std::size_t elementEntries, std::size_t textEntries, std::size_t blipEntries) {
    return 8 + varUintBits(snapshot.sequence) +
           varUintBits(snapshot.baseline == 0 ? 0 : snapshot.sequence - snapshot.baseline) +
           varUintBits(snapshot.step) + varUintBits(entries) + varUintBits(hudEntries) + varUintBits(elementEntries) +
           varUintBits(textEntries) + varUintBits(blipEntries);
}

} // namespace coney::net
