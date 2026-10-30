// SPDX-License-Identifier: GPL-3.0-or-later
// The Snapshot message's body on the wire (docs/guides/snapshots.md#packet), apart from protocol.cpp: its rules, its
// entries' changed bits and words, and the bits the server's budget counts.
#include <cstddef>
#include <cstdint>
#include <expected>

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
    if (const char* broken = snapshotRuleBroken(snapshot)) {
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

std::size_t snapshotHeaderBits(const Snapshot& snapshot, std::size_t entries) {
    return 8 + varUintBits(snapshot.sequence) +
           varUintBits(snapshot.baseline == 0 ? 0 : snapshot.sequence - snapshot.baseline) +
           varUintBits(snapshot.step) + varUintBits(entries);
}

} // namespace coney::net
