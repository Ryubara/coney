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
#include "core/pad.h"
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

// Expected: unverified, #547 (Coney's own protocol; the original has no network play)
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
    const auto timing = std::get<InputTiming>(roundTrip(InputTiming{.lead = -3, .target = 2}));
    CHECK(timing.lead == -3);
    CHECK(timing.target == 2);
}

// Expected: unverified, #547 (Coney's own protocol; the original has no network play)
TEST_CASE("input commands round-trip every field of a connected pad", "[net][protocol]") {
    coney::PadSample pad;
    pad.connected = true;
    pad.buttons = coney::pad::kCross | coney::pad::kL2 | coney::pad::kLeft;
    pad.sticks = {0, 255, 31, 200};
    pad.pressure.at(static_cast<std::size_t>(coney::pad::Pressure::Cross)) = 255;
    pad.pressure.at(static_cast<std::size_t>(coney::pad::Pressure::L2)) = 17;
    const auto commands = std::get<InputCommands>(roundTrip(InputCommands{.newestStep = 123456, .pads = {pad}}));
    CHECK(commands.newestStep == 123456);
    REQUIRE(commands.pads.size() == 1);
    CHECK(commands.pads[0].connected);
    CHECK(commands.pads[0].buttons == pad.buttons);
    CHECK(commands.pads[0].sticks == pad.sticks);
    CHECK(commands.pads[0].pressure == pad.pressure);
}

// Expected: unverified, #547 (Coney's own protocol; the original has no network play)
TEST_CASE("input commands carry the steps before the newest, a repeated pad as one bit", "[net][protocol]") {
    coney::PadSample cross;
    cross.connected = true;
    cross.buttons = coney::pad::kCross;
    coney::PadSample circle = cross;
    circle.buttons = coney::pad::kCircle;
    const InputCommands sent{.newestStep = 40, .pads = {cross, cross, circle, coney::PadSample{}}};
    const auto commands = std::get<InputCommands>(roundTrip(sent));
    CHECK(commands.newestStep == 40);
    REQUIRE(commands.pads.size() == 4);
    CHECK(commands.pads[0].buttons == coney::pad::kCross);
    CHECK(commands.pads[1].buttons == coney::pad::kCross);
    CHECK(commands.pads[2].buttons == coney::pad::kCircle);
    CHECK_FALSE(commands.pads[3].connected);
    // The repeat is one bit: the same message with step 39's pad changed is longer.
    InputCommands changed = sent;
    changed.pads[1].buttons = coney::pad::kSquare;
    CHECK(encode(sent).size() < encode(changed).size());
}

// Expected: unverified, #547 (Coney's own protocol; the original has no network play)
TEST_CASE("a disconnected pad travels as one bit and comes back as a pad at rest", "[net][protocol]") {
    coney::PadSample pad;
    pad.connected = false;
    pad.buttons = coney::pad::kStart; // ignored: a disconnected sample's other fields mean nothing
    pad.sticks = {1, 2, 3, 4};
    const auto commands = std::get<InputCommands>(roundTrip(InputCommands{.newestStep = 1, .pads = {pad}}));
    REQUIRE(commands.pads.size() == 1);
    CHECK_FALSE(commands.pads[0].connected);
    CHECK(commands.pads[0].buttons == 0);
    CHECK(commands.pads[0].sticks == coney::PadSample{}.sticks);
    // Id, the step, then 3 bits of count and one bit of pad, padded to a byte.
    CHECK(encode(InputCommands{.newestStep = 1, .pads = {pad}}) == bytes({0x08, 0x01, 0x00}));
    CHECK(samePadOnTheWire(pad, coney::PadSample{}));
}

// Expected: unverified, #547 (Coney's own protocol; the original has no network play)
TEST_CASE("the encoding is pinned: a change to it must bump kProtocolVersion", "[net][protocol]") {
    // Hello: id 1, the magic "CONY", version 1 (16 bits), the name as a count and its bytes.
    CHECK(encode(Hello{.version = 1, .playerName = "Ax"}) ==
          bytes({0x01, 'C', 'O', 'N', 'Y', 0x01, 0x00, 0x02, 'A', 'x'}));
    // Ping: id 5, the sequence as a varint (300 is two groups: 0x2c with "more", then 0x02).
    CHECK(encode(Ping{.sequence = 300}) == bytes({0x05, 0xac, 0x02}));
    CHECK(encode(Bye{.reason = ByeReason::Quit}) == bytes({0x04, 0x01}));
    // InputCommands: id 8, the newest step as a varint, then from bit 0: the count less one (3 bits, 1 here); the
    // newest pad in full: connected (1), the button word (16 bits, START is bit 11), the four stick bytes (centred,
    // 0x80) and one "pressed" bit per pressure byte (none here); then the older step's "same pad" bit (set).
    coney::PadSample start;
    start.connected = true;
    start.buttons = coney::pad::kStart;
    CHECK(encode(InputCommands{.newestStep = 5, .pads = {start, start}}) ==
          bytes({0x08, 0x05, 0x09, 0x80, 0x00, 0x08, 0x08, 0x08, 0x08, 0x00, 0x01}));
    // InputTiming: id 9, the lead as a zigzag varint (2 is 4), the target (8 bits).
    CHECK(encode(InputTiming{.lead = 2, .target = 1}) == bytes({0x09, 0x04, 0x01}));
    CHECK(kProtocolVersion == 4);
}

// Expected: unverified, #547 (Coney's own protocol; the original has no network play)
TEST_CASE("handshake and Bye go reliably, Ping, Pong and input unreliably", "[net][protocol]") {
    CHECK(deliveryOf(Hello{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(Welcome{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(Reject{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(Bye{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(Ping{}) == Delivery::Unreliable);
    CHECK(deliveryOf(Pong{}) == Delivery::Unreliable);
    CHECK(deliveryOf(InputCommands{}) == Delivery::Unreliable);
    CHECK(deliveryOf(InputTiming{}) == Delivery::Unreliable);
}

// Expected: unverified, #547 (Coney's own protocol; the original has no network play)
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
    refused(bytes({0x07, 0x01, 0x00}), ErrorCode::Invalid);                           // PadInput is retired
    refused(bytes({0x08, 0x01}), ErrorCode::Truncated);                               // input commands with no pad
    refused(bytes({0x08, 0x01, 0x10}), ErrorCode::Invalid); // a disconnected pad with set padding bits
    refused(bytes({0x08, 0x00, 0x01}), ErrorCode::Invalid); // two pads at step 0: one is for step -1
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
    // A pressure byte flagged as pressed but written as 0: one encoding per pad, so it is refused.
    BitWriter zeroPressure;
    zeroPressure.writeU8(static_cast<std::uint8_t>(MessageId::InputCommands));
    zeroPressure.writeVarUint(1);
    zeroPressure.writeBits(0, 3);
    zeroPressure.writeBool(true);
    zeroPressure.writeU16(0);
    zeroPressure.writeU32(0x80808080U);
    zeroPressure.writeBool(true);
    zeroPressure.writeU8(0);
    for (std::size_t i = 1; i < coney::pad::kPressureCount; ++i) {
        zeroPressure.writeBool(false);
    }
    refused(zeroPressure.take(), ErrorCode::Invalid);
    // An older pad written in full that is the same as the step after it: it must be the one "same" bit.
    BitWriter repeated;
    repeated.writeU8(static_cast<std::uint8_t>(MessageId::InputCommands));
    repeated.writeVarUint(1);
    repeated.writeBits(1, 3);
    repeated.writeBool(false); // step 1: unplugged
    repeated.writeBool(false); // step 0: not "same"...
    repeated.writeBool(false); // ...yet unplugged in full
    refused(repeated.take(), ErrorCode::Invalid);
}

// Expected: unverified, #547 (Coney's own protocol; the original has no network play)
TEST_CASE("a Hello of another version decodes to its version, whatever follows", "[net][protocol]") {
    // A later version with a layout this version cannot know.
    const auto decoded = decode(bytes({0x01, 'C', 'O', 'N', 'Y', kProtocolVersion + 1, 0x00, 0xff, 0xff, 0xff}));
    REQUIRE(decoded.has_value());
    const auto& hello = std::get<Hello>(*decoded);
    CHECK(hello.version == kProtocolVersion + 1);
    CHECK(hello.playerName.empty());
}

// Expected: unverified, #547 (Coney's own protocol; the original has no network play)
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
