// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/error.h"
#include "net/transport.h"

// The messages Coney's server and clients exchange, and their exact encoding. Coney's own; the original has no
// network play. Every message is one packet: a message id byte, then its fields through BitWriter, padded with zero
// bits to a whole byte. Decoding is strict: an unknown id, a field out of range, a short packet or trailing bytes
// are an Error, never a guess (docs/guides/multiplayer.md#protocol).

namespace coney::net {

/// The protocol's version. Bump it with any change to a message's layout or meaning: a client and a server of
/// different versions refuse each other in the handshake rather than misread each other.
inline constexpr std::uint16_t kProtocolVersion = 1;
/// The first field of Hello, "CONY" read as a little-endian number: a packet from another program is refused.
inline constexpr std::uint32_t kProtocolMagic = 0x594e4f43U;
/// The UDP port a server listens on unless told otherwise.
inline constexpr std::uint16_t kDefaultPort = 27615;
/// The longest player or server name, in bytes.
inline constexpr std::size_t kMaxNameBytes = 32;

/// The first byte of every message. Values are never reused for another meaning.
enum class MessageId : std::uint8_t {
    Hello = 1,   ///< client to server: the first message, asking to join
    Welcome = 2, ///< server to client: joined
    Reject = 3,  ///< server to client: not joined, and why; the server then closes the connection
    Bye = 4,     ///< either way: the sender is closing the connection, and why
    Ping = 5,    ///< either way: answer with a Pong carrying the same sequence
    Pong = 6,    ///< either way: the answer to a Ping
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

/// Any message.
using Message = std::variant<Hello, Welcome, Reject, Bye, Ping, Pong>;

/// The packet for `message`. A Hello or Welcome whose name is over kMaxNameBytes is a programmer error.
[[nodiscard]] std::vector<std::byte> encode(const Message& message);

/// Reads one packet. Fails with Truncated when it is short and Invalid for anything else wrong: an unknown id or
/// enumerator, the wrong magic, a name over kMaxNameBytes, trailing bytes. A Hello of another version decodes to its
/// version alone (an empty name), since the rest of its layout is that version's.
[[nodiscard]] std::expected<Message, Error> decode(std::span<const std::byte> packet);

/// How a message travels: the handshake and Bye reliably and in order, Ping and Pong unreliably.
[[nodiscard]] Delivery deliveryOf(const Message& message);

/// Whether `name` may be a player's or a server's name: 1 to kMaxNameBytes printable ASCII characters (space to
/// tilde), not all spaces.
[[nodiscard]] bool validPlayerName(std::string_view name);

/// Short English text for the Network page and the log.
[[nodiscard]] std::string_view describe(RejectReason reason);
[[nodiscard]] std::string_view describe(ByeReason reason);

} // namespace coney::net
