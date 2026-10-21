// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/protocol.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "core/error.h"
#include "core/pad.h"
#include "net/bit_stream.h"
#include "net/transport.h"

namespace coney::net {

namespace {

// Writes a pad as PadInput's comment lays it out: a disconnected pad is one bit.
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
            } else {
                static_assert(std::is_same_v<T, PadInput>);
                out.writeU8(static_cast<std::uint8_t>(MessageId::PadInput));
                out.writeVarUint(m.step);
                writePad(out, m.pad);
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
    case MessageId::PadInput: {
        auto step = readSequence(in);
        if (!step) {
            return std::unexpected(step.error());
        }
        auto pad = readPad(in);
        if (!pad) {
            return std::unexpected(pad.error());
        }
        return PadInput{.step = *step, .pad = *pad};
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
    // A pad input is sent again every step, so a lost one is replaced at once; a resend would only arrive stale.
    if (std::holds_alternative<Ping>(message) || std::holds_alternative<Pong>(message) ||
        std::holds_alternative<PadInput>(message)) {
        return Delivery::Unreliable;
    }
    return Delivery::ReliableOrdered;
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
