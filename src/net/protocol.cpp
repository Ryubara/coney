// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/protocol.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "core/assert.h"
#include "core/error.h"
#include "core/pad.h"
#include "net/bit_stream.h"
#include "net/net_id.h"
#include "net/replicated_fields.h"
#include "net/transport.h"

namespace coney::net {

namespace {

// Writes a pad in full as InputCommands' comment lays it out: a disconnected pad is one bit.
void writePad(BitWriter& out, const PadSample& pad) {
    out.writeBool(pad.connected);
    if (!pad.connected) {
        return;
    }
    out.writeU16(pad.buttons);
    for (const std::uint8_t stick : pad.sticks) {
        out.writeU8(stick);
    }
    // Most pressure bytes are 0 at any moment (a button not held), so each costs one bit then.
    for (const std::uint8_t pressure : pad.pressure) {
        out.writeBool(pressure != 0);
        if (pressure != 0) {
            out.writeU8(pressure);
        }
    }
}

// Reads a pad written by writePad(); a pressure flagged as above 0 but written as 0 is Invalid.
std::expected<PadSample, Error> readPad(BitReader& in) {
    PadSample pad;
    auto connected = in.readBool();
    if (!connected) {
        return std::unexpected(connected.error());
    }
    if (!*connected) {
        return pad;
    }
    pad.connected = true;
    auto buttons = in.readU16();
    if (!buttons) {
        return std::unexpected(buttons.error());
    }
    pad.buttons = *buttons;
    for (std::uint8_t& stick : pad.sticks) {
        auto value = in.readU8();
        if (!value) {
            return std::unexpected(value.error());
        }
        stick = *value;
    }
    for (std::uint8_t& pressure : pad.pressure) {
        auto pressed = in.readBool();
        if (!pressed) {
            return std::unexpected(pressed.error());
        }
        if (!*pressed) {
            continue;
        }
        auto value = in.readU8();
        if (!value) {
            return std::unexpected(value.error());
        }
        if (*value == 0) {
            return fail(ErrorCode::Invalid, "network pad has a pressure flagged as pressed but 0");
        }
        pressure = *value;
    }
    return pad;
}

// Every human field's changed bit set: what an entry for a human the baseline does not hold carries.
constexpr std::uint32_t kAllHumanFields = (1U << kHumanFieldCount) - 1U;
static_assert(kHumanFieldCount < 32);

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

// Writes one message's id and fields.
void writeMessage(BitWriter& out, const Message& message) {
    std::visit(
        [&](const auto& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, Hello>) {
                out.writeU8(static_cast<std::uint8_t>(MessageId::Hello));
                out.writeU32(kProtocolMagic);
                out.writeU16(m.version);
                out.writeString(m.playerName, kMaxNameBytes);
            } else if constexpr (std::is_same_v<T, Welcome>) {
                out.writeU8(static_cast<std::uint8_t>(MessageId::Welcome));
                out.writeU8(m.playerSlot);
                out.writeU16(m.stepRateHz);
                out.writeString(m.serverName, kMaxNameBytes);
            } else if constexpr (std::is_same_v<T, Reject>) {
                out.writeU8(static_cast<std::uint8_t>(MessageId::Reject));
                out.writeU8(static_cast<std::uint8_t>(m.reason));
                out.writeU16(m.serverVersion);
            } else if constexpr (std::is_same_v<T, Bye>) {
                out.writeU8(static_cast<std::uint8_t>(MessageId::Bye));
                out.writeU8(static_cast<std::uint8_t>(m.reason));
            } else if constexpr (std::is_same_v<T, Ping>) {
                out.writeU8(static_cast<std::uint8_t>(MessageId::Ping));
                out.writeVarUint(m.sequence);
            } else if constexpr (std::is_same_v<T, Pong>) {
                out.writeU8(static_cast<std::uint8_t>(MessageId::Pong));
                out.writeVarUint(m.sequence);
            } else if constexpr (std::is_same_v<T, Snapshot>) {
                CONEY_ASSERT(snapshotRuleBroken(m) == nullptr);
                out.writeU8(static_cast<std::uint8_t>(MessageId::Snapshot));
                out.writeVarUint(m.sequence);
                out.writeVarUint(m.baseline == 0 ? 0 : m.sequence - m.baseline);
                out.writeVarUint(m.step);
                out.writeVarUint(m.humans.size());
                for (const SnapshotEntry& entry : m.humans) {
                    writeSnapshotEntry(out, entry);
                }
            } else if constexpr (std::is_same_v<T, SnapshotAck>) {
                CONEY_ASSERT(m.sequence != 0);
                out.writeU8(static_cast<std::uint8_t>(MessageId::SnapshotAck));
                out.writeVarUint(m.sequence);
            } else if constexpr (std::is_same_v<T, InputTiming>) {
                out.writeU8(static_cast<std::uint8_t>(MessageId::InputTiming));
                out.writeVarInt(m.lead);
                out.writeU8(m.target);
            } else {
                static_assert(std::is_same_v<T, InputCommands>);
                CONEY_ASSERT(!m.pads.empty() && m.pads.size() <= kMaxInputCommands);
                CONEY_ASSERT(m.pads.size() - 1 <= m.newestStep);
                out.writeU8(static_cast<std::uint8_t>(MessageId::InputCommands));
                out.writeVarUint(m.newestStep);
                out.writeBits(static_cast<std::uint32_t>(m.pads.size() - 1), 3);
                writePad(out, m.pads.front());
                // A held button or stick is usually held for several steps, so an older pad is often one bit.
                for (std::size_t i = 1; i < m.pads.size(); ++i) {
                    const bool same = samePadOnTheWire(m.pads[i], m.pads[i - 1]);
                    out.writeBool(same);
                    if (!same) {
                        writePad(out, m.pads[i]);
                    }
                }
            }
        },
        message);
}

// Reads a 32-bit sequence number written with writeVarUint; larger is Invalid.
std::expected<std::uint32_t, Error> readSequence(BitReader& in) {
    auto value = in.readVarUint();
    if (!value) {
        return std::unexpected(value.error());
    }
    if (*value > 0xffffffffU) {
        return fail(ErrorCode::Invalid, "network sequence is over 32 bits");
    }
    return static_cast<std::uint32_t>(*value);
}

// Reads the fields of the message `id` names; the caller checks for trailing bytes.
std::expected<Message, Error> readFields(BitReader& in, std::uint8_t id) {
    switch (static_cast<MessageId>(id)) {
    case MessageId::Hello: {
        auto magic = in.readU32();
        if (!magic) {
            return std::unexpected(magic.error());
        }
        if (*magic != kProtocolMagic) {
            return fail(ErrorCode::Invalid, "network hello is not from Coney");
        }
        auto version = in.readU16();
        if (!version) {
            return std::unexpected(version.error());
        }
        if (*version != kProtocolVersion) {
            // Another version's layout follows; the version is all the server needs to refuse it.
            return Hello{.version = *version, .playerName = {}};
        }
        auto name = in.readString(kMaxNameBytes);
        if (!name) {
            return std::unexpected(name.error());
        }
        return Hello{.version = *version, .playerName = std::move(*name)};
    }
    case MessageId::Welcome: {
        auto slot = in.readU8();
        if (!slot) {
            return std::unexpected(slot.error());
        }
        auto rate = in.readU16();
        if (!rate) {
            return std::unexpected(rate.error());
        }
        auto name = in.readString(kMaxNameBytes);
        if (!name) {
            return std::unexpected(name.error());
        }
        if (*rate == 0) {
            return fail(ErrorCode::Invalid, "network welcome has a step rate of 0");
        }
        return Welcome{.playerSlot = *slot, .stepRateHz = *rate, .serverName = std::move(*name)};
    }
    case MessageId::Reject: {
        auto reason = in.readU8();
        if (!reason) {
            return std::unexpected(reason.error());
        }
        auto version = in.readU16();
        if (!version) {
            return std::unexpected(version.error());
        }
        if (*reason < static_cast<std::uint8_t>(RejectReason::VersionMismatch) ||
            *reason > static_cast<std::uint8_t>(RejectReason::BadName)) {
            return fail(ErrorCode::Invalid, "network reject has an unknown reason");
        }
        return Reject{.reason = static_cast<RejectReason>(*reason), .serverVersion = *version};
    }
    case MessageId::Bye: {
        auto reason = in.readU8();
        if (!reason) {
            return std::unexpected(reason.error());
        }
        if (*reason < static_cast<std::uint8_t>(ByeReason::Quit) ||
            *reason > static_cast<std::uint8_t>(ByeReason::ProtocolError)) {
            return fail(ErrorCode::Invalid, "network bye has an unknown reason");
        }
        return Bye{.reason = static_cast<ByeReason>(*reason)};
    }
    case MessageId::Ping: {
        auto sequence = readSequence(in);
        if (!sequence) {
            return std::unexpected(sequence.error());
        }
        return Ping{.sequence = *sequence};
    }
    case MessageId::Pong: {
        auto sequence = readSequence(in);
        if (!sequence) {
            return std::unexpected(sequence.error());
        }
        return Pong{.sequence = *sequence};
    }
    case MessageId::InputCommands: {
        auto newest = readSequence(in);
        if (!newest) {
            return std::unexpected(newest.error());
        }
        auto countLess1 = in.readBits(3);
        if (!countLess1) {
            return std::unexpected(countLess1.error());
        }
        if (*countLess1 > *newest) {
            return fail(ErrorCode::Invalid, "network input commands reach below step 0");
        }
        InputCommands commands{.newestStep = *newest, .pads = {}};
        auto first = readPad(in);
        if (!first) {
            return std::unexpected(first.error());
        }
        commands.pads.push_back(*first);
        for (std::uint32_t i = 0; i < *countLess1; ++i) {
            auto same = in.readBool();
            if (!same) {
                return std::unexpected(same.error());
            }
            if (*same) {
                commands.pads.push_back(commands.pads.back());
                continue;
            }
            auto pad = readPad(in);
            if (!pad) {
                return std::unexpected(pad.error());
            }
            // One encoding per message: a pad the same as the step after it must be the one bit.
            if (samePadOnTheWire(*pad, commands.pads.back())) {
                return fail(ErrorCode::Invalid, "network input command repeats a pad in full");
            }
            commands.pads.push_back(*pad);
        }
        return commands;
    }
    case MessageId::Snapshot: {
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
    case MessageId::SnapshotAck: {
        auto sequence = readSequence(in);
        if (!sequence) {
            return std::unexpected(sequence.error());
        }
        if (*sequence == 0) {
            return fail(ErrorCode::Invalid, "network snapshot acknowledgement names sequence 0");
        }
        return SnapshotAck{.sequence = *sequence};
    }
    case MessageId::InputTiming: {
        auto lead = in.readVarInt();
        if (!lead) {
            return std::unexpected(lead.error());
        }
        if (*lead < std::numeric_limits<std::int32_t>::min() || *lead > std::numeric_limits<std::int32_t>::max()) {
            return fail(ErrorCode::Invalid, "network input lead is over 32 bits");
        }
        auto target = in.readU8();
        if (!target) {
            return std::unexpected(target.error());
        }
        return InputTiming{.lead = static_cast<std::int32_t>(*lead), .target = *target};
    }
    }
    return fail(ErrorCode::Invalid, "network message has an unknown id " + std::to_string(id));
}

} // namespace

std::vector<std::byte> encode(const Message& message) {
    BitWriter out;
    writeMessage(out, message);
    return out.take();
}

std::expected<Message, Error> decode(std::span<const std::byte> packet) {
    BitReader in(packet);
    auto id = in.readU8();
    if (!id) {
        return std::unexpected(id.error());
    }
    auto message = readFields(in, *id);
    if (!message) {
        return message;
    }
    // A Hello of another version stops early by design; anything else must end where its fields do.
    const auto* hello = std::get_if<Hello>(&*message);
    if (hello == nullptr || hello->version == kProtocolVersion) {
        if (auto finished = in.finish(); !finished) {
            return std::unexpected(finished.error());
        }
    }
    return message;
}

Delivery deliveryOf(const Message& message) {
    // Input commands repeat the steps before them in every packet, and timing goes with every Ping, so a lost one is
    // replaced at once; a resend would only arrive stale.
    if (std::holds_alternative<Ping>(message) || std::holds_alternative<Pong>(message) ||
        std::holds_alternative<InputCommands>(message) || std::holds_alternative<InputTiming>(message) ||
        std::holds_alternative<SnapshotAck>(message)) {
        return Delivery::Unreliable;
    }
    // A snapshot older than one already delivered is of no use: the newer one holds everything it does.
    if (std::holds_alternative<Snapshot>(message)) {
        return Delivery::UnreliableSequenced;
    }
    return Delivery::ReliableOrdered;
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

bool samePadOnTheWire(const PadSample& a, const PadSample& b) {
    if (!a.connected || !b.connected) {
        return a.connected == b.connected; // a disconnected pad's other fields never travel
    }
    return a.buttons == b.buttons && a.sticks == b.sticks && a.pressure == b.pressure;
}

bool validPlayerName(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameBytes) {
        return false;
    }
    bool visible = false;
    for (const char c : name) {
        if (c < ' ' || c > '~') {
            return false;
        }
        visible = visible || c != ' ';
    }
    return visible;
}

std::string_view describe(RejectReason reason) {
    switch (reason) {
    case RejectReason::VersionMismatch:
        return "different Coney version";
    case RejectReason::ServerFull:
        return "server full";
    case RejectReason::BadName:
        return "bad player name";
    }
    return "refused";
}

std::string_view describe(ByeReason reason) {
    switch (reason) {
    case ByeReason::Quit:
        return "left";
    case ByeReason::TimedOut:
        return "timed out";
    case ByeReason::ProtocolError:
        return "protocol error";
    }
    return "closed";
}

} // namespace coney::net
