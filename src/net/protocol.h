// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/error.h"
#include "core/pad.h"
#include "net/net_id.h"
#include "net/replicated_fields.h"
#include "net/transport.h"

// The messages Coney's server and clients exchange, and their exact encoding. Coney's own; the original has no
// network play. Every message is one packet: a message id byte, then its fields through BitWriter, padded with zero
// bits to a whole byte. Decoding is strict: an unknown id, a field out of range, a short packet or trailing bytes
// are an Error, never a guess (docs/guides/multiplayer.md#protocol).

namespace coney::net {

/// The protocol's version. Bump it with any change to a message's layout or meaning: a client and a server of
/// different versions refuse each other in the handshake rather than misread each other.
inline constexpr std::uint16_t kProtocolVersion = 4;
/// The first field of Hello, "CONY" read as a little-endian number: a packet from another program is refused.
inline constexpr std::uint32_t kProtocolMagic = 0x594e4f43U;
/// The UDP port a server listens on unless told otherwise.
inline constexpr std::uint16_t kDefaultPort = 27615;
/// The longest player or server name, in bytes.
inline constexpr std::size_t kMaxNameBytes = 32;
/// The most steps one InputCommands carries: its newest and up to seven before it.
inline constexpr std::size_t kMaxInputCommands = 8;

/// The first byte of every message. Values are never reused for another meaning.
enum class MessageId : std::uint8_t {
    Hello = 1,   ///< client to server: the first message, asking to join
    Welcome = 2, ///< server to client: joined
    Reject = 3,  ///< server to client: not joined, and why; the server then closes the connection
    Bye = 4,     ///< either way: the sender is closing the connection, and why
    Ping = 5,    ///< either way: answer with a Pong carrying the same sequence
    Pong = 6,    ///< either way: the answer to a Ping
    // 7 was PadInput (protocol 2: one step's pad per packet), replaced by InputCommands; never reused.
    InputCommands = 8, ///< client to server: the client's pads in its latest few steps (protocol 3)
    InputTiming = 9,   ///< server to client: how early the client's input commands arrive (protocol 3)
    Snapshot = 10,     ///< server to client: the humans the client needs, as a delta (protocol 4)
    SnapshotAck = 11   ///< client to server: the newest snapshot the client has decoded (protocol 4)
};

/// Why a server refused a Hello.
enum class RejectReason : std::uint8_t {
    VersionMismatch = 1, ///< the client's kProtocolVersion is not the server's
    ServerFull = 2,      ///< every player slot is taken
    BadName = 3,         ///< the player name is empty, too long or has characters other than printable ASCII
};

/// Why a side closes a connection.
enum class ByeReason : std::uint8_t {
    Quit = 1,          ///< the player left, or the host stopped the server
    TimedOut = 2,      ///< nothing heard from the other side for too long
    ProtocolError = 3, ///< the other side sent a message that does not decode, or one out of turn
};

/// Asks to join. Its first two fields keep this layout in every protocol version, so a server can always read a
/// client's version and answer VersionMismatch.
struct Hello {
    std::uint16_t version = kProtocolVersion;
    std::string playerName; ///< validPlayerName(); not checked by decode(), so the server can answer BadName
};
/// Says the client has joined.
struct Welcome {
    std::uint8_t playerSlot = 0;   ///< the client's player: 1 for the first remote player (the host is 0)
    std::uint16_t stepRateHz = 30; ///< the server's fixed simulation steps a second
    std::string serverName;
};
/// Refuses a Hello.
struct Reject {
    RejectReason reason = RejectReason::VersionMismatch;
    std::uint16_t serverVersion = kProtocolVersion;
};
/// Announces a close.
struct Bye {
    ByeReason reason = ByeReason::Quit;
};
/// Measures the round trip; sent unreliably, so a lost one counts towards the loss figure.
struct Ping {
    std::uint32_t sequence = 0;
};
/// Answers a Ping.
struct Pong {
    std::uint32_t sequence = 0;
};

/// The client's pads in its latest few steps, newest first: what its player 1 pad gave in each, which the host's game
/// reads as its pad port 2. Sent every step, unreliably; each repeats the steps before it, so a lost packet costs
/// nothing as long as the next one arrives, and the server's InputBuffer applies each step's pad on a step of its
/// own (docs/guides/multiplayer.md#input-commands).
///
/// The packet: the newest step (a varint), the number of pads less one (3 bits), the newest step's pad in full, then
/// for each older step a bit saying it is the same pad as the step after it and, when it is not, that pad in full.
/// A pad in full is: connected (1 bit); then, only when connected, the button word (16 bits), the four stick bytes
/// (8 bits each, PadSample::sticks order) and, for each of the 12 pressure bytes, a bit saying whether it is above 0
/// and then the byte when it is. A disconnected pad decodes as PadSample{} (sticks centred, nothing held).
struct InputCommands {
    std::uint32_t newestStep = 0; ///< the client's steps since its Welcome, counted from 0
    /// pads[i] is step newestStep - i's: 1 to kMaxInputCommands of them, none for a step below 0.
    std::vector<PadSample> pads;
};

/// How early a client's input commands reach the server, so the client can send a little earlier or later. Sent
/// with each Ping, unreliably (docs/guides/multiplayer.md#input-timing).
///
/// The packet: the lead (a zigzag varint, 32 bits at most), then the target (8 bits).
struct InputTiming {
    /// The client's steps waiting in the server's buffer after its latest step (InputBuffer::lead()): below
    /// `target` the commands come late, above it they wait longer than they need to.
    std::int32_t lead = 0;
    std::uint8_t target = 1; ///< the lead the server wants: its InputBufferConfig::delaySteps
};

/// How many snapshots each side keeps: the server the last it sent to each client, the client the last it decoded. A
/// snapshot's baseline is one of the kSnapshotHistory before it (docs/guides/network-architecture.md#snapshots).
inline constexpr std::uint32_t kSnapshotHistory = 32;

/// The fields of a human (fieldTable(ReplicatedKind::Human)), in HumanField order.
inline constexpr std::size_t kHumanFieldCount = static_cast<std::size_t>(HumanField::Count);

/// Every human field's changed bit set (SnapshotEntry::changed): what an entry for a human the baseline does not hold
/// carries.
inline constexpr std::uint32_t kAllHumanFields = (1U << kHumanFieldCount) - 1U;
static_assert(kHumanFieldCount < 32, "a SnapshotEntry's changed bits are one 32-bit word");

/// A human's fields as they travel: each field's fieldWord(), in HumanField order.
using HumanWords = std::array<std::uint32_t, kHumanFieldCount>;

/// One human in a Snapshot: gone, or the fields that differ from the baseline's (every field for a human the
/// baseline does not hold).
struct SnapshotEntry {
    NetId id;             ///< valid()
    bool removed = false; ///< the human has gone (or is no longer the client's); `changed` is 0
    /// Bit i set: field i is written; 0 only for a removal. A human the baseline does not hold has every bit set.
    std::uint32_t changed = 0;
    HumanWords words{}; ///< each changed field's word; the others are 0

    friend bool operator==(const SnapshotEntry&, const SnapshotEntry&) = default;
};

/// The state of the humans a client needs, as a delta against a snapshot it has acknowledged (its baseline), or in
/// full when it has none. Sent every step, UnreliableSequenced: a lost one is repaired by the next, which is a delta
/// against an older baseline (docs/guides/snapshots.md).
///
/// The packet: the sequence (a varint, 32 bits at most), how many snapshots back the baseline is (a varint, 0 for
/// none, else 1 to kSnapshotHistory and no further back than the first), the server's step (a varint, 32 bits at
/// most), the number of entries (a varint), then each entry: its NetId (kNetIdBits), a removed bit and, when not
/// removed, for each human field a changed bit followed by the field's word (its bits) when set.
struct Snapshot {
    std::uint32_t sequence = 1;        ///< 1 for a client's first, one more for each after
    std::uint32_t baseline = 0;        ///< the sequence it is a delta against; 0 for none (a full snapshot)
    std::uint32_t step = 0;            ///< the server's step it shows
    std::vector<SnapshotEntry> humans; ///< in strictly rising id order

    friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

/// The newest snapshot the client has decoded, which the server's next snapshots may take as their baseline. Sent
/// unreliably after each step that decoded one; a lost one is replaced by the next.
///
/// The packet: the sequence (a varint, 32 bits at most).
struct SnapshotAck {
    std::uint32_t sequence = 0;
};

/// Bits `entry` takes in a Snapshot's packet: what the server's snapshot budget counts.
[[nodiscard]] std::size_t snapshotEntryBits(const SnapshotEntry& entry);

/// Bits a Snapshot's packet takes before its entries (the message id included) with `entries` entries.
[[nodiscard]] std::size_t snapshotHeaderBits(const Snapshot& snapshot, std::size_t entries);

/// Whether two pads travel as the same bits: both disconnected, or both connected with the same buttons, sticks and
/// pressures.
[[nodiscard]] bool samePadOnTheWire(const PadSample& a, const PadSample& b);

/// Any message.
using Message =
    std::variant<Hello, Welcome, Reject, Bye, Ping, Pong, InputCommands, InputTiming, Snapshot, SnapshotAck>;

/// The packet for `message`. A Hello or Welcome whose name is over kMaxNameBytes, an InputCommands with no pads, more
/// than kMaxInputCommands or one for a step below 0, and a Snapshot that breaks its rules (sequence 0, a baseline not
/// among the kSnapshotHistory before it, ids not valid and rising, a kept entry with nothing changed, a removal with
/// something) are programmer errors.
[[nodiscard]] std::vector<std::byte> encode(const Message& message);

/// Reads one packet. Fails with Truncated when it is short and Invalid for anything else wrong: an unknown id or
/// enumerator, the wrong magic, a name over kMaxNameBytes, a pressure byte flagged as above 0 but written as 0, an
/// older pad written in full that is the same as the step after it, a pad for a step below 0, a Snapshot that breaks
/// the rules encode() asserts or has a field word over its range, trailing bytes. A Hello
/// of another version decodes to its version alone (an empty name), since the rest of its layout is that version's.
[[nodiscard]] std::expected<Message, Error> decode(std::span<const std::byte> packet);

/// How a message travels: the handshake and Bye reliably and in order, a Snapshot unreliably but never older after
/// newer (UnreliableSequenced), Ping, Pong, InputCommands, InputTiming and SnapshotAck unreliably.
[[nodiscard]] Delivery deliveryOf(const Message& message);

/// Whether `name` may be a player's or a server's name: 1 to kMaxNameBytes printable ASCII characters (space to
/// tilde), not all spaces.
[[nodiscard]] bool validPlayerName(std::string_view name);

/// Short English text for the Network page and the log.
[[nodiscard]] std::string_view describe(RejectReason reason);
[[nodiscard]] std::string_view describe(ByeReason reason);

} // namespace coney::net
