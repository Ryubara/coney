// SPDX-License-Identifier: GPL-3.0-or-later
// The session messages (docs/guides/multiplayer.md#protocol): every message round-trips, the encoding is pinned byte
// for byte, and decoding refuses whatever is not exactly a message of this version.
#include "net/protocol.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "net/bit_stream.h"
#include "net/transport.h"

using coney::ErrorCode;
using namespace coney::net;

namespace {

// Encodes and decodes `message`, which must decode.
Message roundTrip(const Message& message) {
    auto decoded = decode(encode(message));
    REQUIRE(decoded.has_value());
    return *decoded;
}

// Bytes from a list of numbers, for hand-built packets.
std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

} // namespace

TEST_CASE("every message round-trips", "[net][protocol]") {
    const auto hello = std::get<Hello>(roundTrip(Hello{.version = kProtocolVersion, .playerName = "Cleon"}));
    CHECK(hello.version == kProtocolVersion);
    CHECK(hello.playerName == "Cleon");
    const auto welcome =
        std::get<Welcome>(roundTrip(Welcome{.playerSlot = 2, .stepRateHz = 30, .serverName = "Coney Island"}));
    CHECK(welcome.playerSlot == 2);
    CHECK(welcome.stepRateHz == 30);
    CHECK(welcome.serverName == "Coney Island");
    const auto reject = std::get<Reject>(roundTrip(Reject{.reason = RejectReason::ServerFull, .serverVersion = 7}));
    CHECK(reject.reason == RejectReason::ServerFull);
    CHECK(reject.serverVersion == 7);
    CHECK(std::get<Bye>(roundTrip(Bye{.reason = ByeReason::TimedOut})).reason == ByeReason::TimedOut);
    CHECK(std::get<Ping>(roundTrip(Ping{.sequence = 0xfffffffeU})).sequence == 0xfffffffeU);
    CHECK(std::get<Pong>(roundTrip(Pong{.sequence = 5})).sequence == 5);
}

TEST_CASE("the encoding is pinned: a change to it must bump kProtocolVersion", "[net][protocol]") {
    // Hello: id 1, the magic "CONY", version 1 (16 bits), the name as a count and its bytes.
    CHECK(encode(Hello{.version = 1, .playerName = "Ax"}) ==
          bytes({0x01, 'C', 'O', 'N', 'Y', 0x01, 0x00, 0x02, 'A', 'x'}));
    // Ping: id 5, the sequence as a varint (300 is two groups: 0x2c with "more", then 0x02).
    CHECK(encode(Ping{.sequence = 300}) == bytes({0x05, 0xac, 0x02}));
    CHECK(encode(Bye{.reason = ByeReason::Quit}) == bytes({0x04, 0x01}));
    CHECK(kProtocolVersion == 1);
}

TEST_CASE("handshake and Bye go reliably, Ping and Pong unreliably", "[net][protocol]") {
    CHECK(deliveryOf(Hello{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(Welcome{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(Reject{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(Bye{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(Ping{}) == Delivery::Unreliable);
    CHECK(deliveryOf(Pong{}) == Delivery::Unreliable);
}

TEST_CASE("a packet that is not exactly a message is refused", "[net][protocol]") {
    const auto refused = [](const std::vector<std::byte>& packet, ErrorCode code) {
        const auto decoded = decode(packet);
        REQUIRE_FALSE(decoded.has_value());
        CHECK(decoded.error().code == code);
    };
    refused({}, ErrorCode::Truncated);
    refused(bytes({0x00}), ErrorCode::Invalid);                                       // id 0 is no message
    refused(bytes({0x63}), ErrorCode::Invalid);                                       // nor is an unknown id
    refused(bytes({0x05}), ErrorCode::Truncated);                                     // a Ping with no sequence
    refused(bytes({0x05, 0x01, 0x00}), ErrorCode::Invalid);                           // a trailing byte
    refused(bytes({0x04, 0x09}), ErrorCode::Invalid);                                 // an unknown Bye reason
    refused(bytes({0x03, 0x00, 0x01, 0x00}), ErrorCode::Invalid);                     // an unknown Reject reason
    refused(bytes({0x02, 0x01, 0x00, 0x00, 0x00}), ErrorCode::Invalid);               // a step rate of 0
    refused(bytes({0x01, 'N', 'O', 'P', 'E', 0x01, 0x00, 0x00}), ErrorCode::Invalid); // not Coney's magic
    // A sequence over 32 bits.
    BitWriter big;
    big.writeU8(static_cast<std::uint8_t>(MessageId::Pong));
    big.writeVarUint(0x100000000ULL);
    refused(big.take(), ErrorCode::Invalid);
    // A name over kMaxNameBytes.
    BitWriter longName;
    longName.writeU8(static_cast<std::uint8_t>(MessageId::Hello));
    longName.writeU32(kProtocolMagic);
    longName.writeU16(kProtocolVersion);
    longName.writeString(std::string(kMaxNameBytes + 1, 'a'), kMaxNameBytes + 1);
    refused(longName.take(), ErrorCode::Invalid);
}

TEST_CASE("a Hello of another version decodes to its version, whatever follows", "[net][protocol]") {
    // Version 2 with a layout this version cannot know.
    const auto decoded = decode(bytes({0x01, 'C', 'O', 'N', 'Y', 0x02, 0x00, 0xff, 0xff, 0xff}));
    REQUIRE(decoded.has_value());
    const auto& hello = std::get<Hello>(*decoded);
    CHECK(hello.version == 2);
    CHECK(hello.playerName.empty());
}

TEST_CASE("player names are 1 to 32 printable ASCII characters, not all spaces", "[net][protocol]") {
    CHECK(validPlayerName("Swan"));
    CHECK(validPlayerName("Ash 2"));
    CHECK(validPlayerName(std::string(kMaxNameBytes, 'x')));
    CHECK_FALSE(validPlayerName(""));
    CHECK_FALSE(validPlayerName("   "));
    CHECK_FALSE(validPlayerName(std::string(kMaxNameBytes + 1, 'x')));
    CHECK_FALSE(validPlayerName("tab\there"));
    CHECK_FALSE(validPlayerName("caf\xc3\xa9"));
    CHECK(describe(RejectReason::ServerFull) == "server full");
    CHECK(describe(ByeReason::TimedOut) == "timed out");
}
