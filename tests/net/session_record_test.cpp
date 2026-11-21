// SPDX-License-Identifier: GPL-3.0-or-later
// A host's session record (docs/guides/multiplayer.md#replays): each step's pad on the remote port and the session's
// events, written as text lines and read back strictly, and the replay's pad for a step.
#include "net/session_record.h"

#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/pad.h"
#include "support/optional_value.h"

using coney::ErrorCode;
using coney::PadSample;
using coney::test::got;
using namespace coney::net;

namespace {

// A pad with something of everything held.
PadSample busyPad() {
    PadSample pad;
    pad.connected = true;
    pad.buttons = coney::pad::kCross | coney::pad::kR1;
    pad.sticks = {0x80, 0x7f, 0x00, 0xff};
    pad.pressure.at(3) = 0x40;
    pad.pressure.at(11) = 0xff;
    return pad;
}

// Whether two pads are the same to the game: an unplugged pad's other bytes do not count.
bool samePad(const PadSample& a, const PadSample& b) {
    if (a.connected != b.connected) {
        return false;
    }
    return !a.connected || (a.buttons == b.buttons && a.sticks == b.sticks && a.pressure == b.pressure);
}

} // namespace

TEST_CASE("a pad line round-trips a plugged and an unplugged pad") {
    // Expected: unverified, #558 (Coney's own file format)
    const std::string busy = formatPadLine(17, busyPad());
    CHECK(busy == "pad 17 1 0048 807f00ff 0000004000000000000000ff");
    const auto read = parseRecordLine(busy);
    REQUIRE(read.has_value());
    REQUIRE(read->kind == RecordLine::Kind::Pad);
    CHECK(read->step == 17);
    CHECK(samePad(read->pad, busyPad()));

    const std::string unplugged = formatPadLine(3, PadSample{});
    CHECK(unplugged == "pad 3 0");
    const auto readUnplugged = parseRecordLine(unplugged);
    REQUIRE(readUnplugged.has_value());
    CHECK_FALSE(readUnplugged->pad.connected);
}

TEST_CASE("an event line keeps its text, and a malformed line is refused") {
    // Expected: unverified, #558 (Coney's own file format)
    const auto event = parseRecordLine(formatEventLine(40, "Tester joined as player 2"));
    REQUIRE(event.has_value());
    CHECK(event->kind == RecordLine::Kind::Event);
    CHECK(event->step == 40);
    CHECK(event->text == "Tester joined as player 2");

    for (const char* bad :
         {"", "pad", "pad x 0", "pad 1", "pad 1 2", "pad 1 0 0048", "pad 1 1 0048 807f00ff",
          "pad 1 1 005 807f00ff 0000004000000000000000ff", "pad 1 1 0048 807f00f 0000004000000000000000ff",
          "pad 1 1 0048 807f00ff 0000004000000000000000f", "pad 1 1 0048 807f00ff 0000004000000000000000fg", "event 1",
          "noise 1 0"}) {
        INFO(bad);
        const auto refused = parseRecordLine(bad);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::Invalid);
    }
}

TEST_CASE("a replay gives each recorded step's pad and nothing for the rest") {
    // Expected: unverified, #558 (Coney's own replay)
    const std::string text =
        formatPadLine(2, busyPad()) + "\n" + formatEventLine(2, "joined") + "\n" + formatPadLine(3, PadSample{}) + "\n";
    const auto replay = SessionReplay::parse(text);
    REQUIRE(replay.has_value());
    CHECK(replay->steps() == 2);
    const auto second = replay->pad(2);
    REQUIRE(second.has_value());
    CHECK(samePad(got(second), busyPad()));
    const auto third = replay->pad(3);
    REQUIRE(third.has_value());
    CHECK_FALSE(got(third).connected);
    CHECK_FALSE(replay->pad(1).has_value());
    CHECK_FALSE(replay->pad(4).has_value());

    // A step recorded twice is refused, with its line number.
    const auto twice = SessionReplay::parse(formatPadLine(2, busyPad()) + "\n" + formatPadLine(2, busyPad()) + "\n");
    REQUIRE_FALSE(twice.has_value());
    CHECK(twice.error().code == ErrorCode::Invalid);
    CHECK(twice.error().message.find("line 2") != std::string::npos);
}

TEST_CASE("a held step is recorded as a hold line, which the replay holds too") {
    // Expected: unverified, #1530 (Coney's own record; docs/guides/multiplayer.md#replays: a step the loading barrier
    // held read its pads and ran nothing)
    CHECK(formatHoldLine(7) == "hold 7");
    const auto line = parseRecordLine("hold 7");
    REQUIRE(line.has_value());
    CHECK(line->kind == RecordLine::Kind::Hold);
    CHECK(line->step == 7);
    CHECK_FALSE(parseRecordLine("hold 7 1").has_value());
    CHECK_FALSE(parseRecordLine("hold").has_value());

    // A held step's pad is still recorded: the pads are read on a held step.
    const auto replay = SessionReplay::parse(formatPadLine(7, busyPad()) + "\n" + formatHoldLine(7) + "\n" +
                                             formatPadLine(8, busyPad()));
    REQUIRE(replay.has_value());
    CHECK(replay->held(7));
    CHECK_FALSE(replay->held(8));
    CHECK(replay->steps() == 2);

    const auto twice = SessionReplay::parse(formatHoldLine(3) + "\n" + formatHoldLine(3) + "\n");
    REQUIRE_FALSE(twice.has_value());
    CHECK(twice.error().message.find("line 2: step 3 is held twice") != std::string::npos);
}
