// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pad.h"

#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/pads.h"

using coney::Pad;
using coney::Pads;
using coney::PadSample;
namespace pad = coney::pad;

namespace {

// A connected sample holding `buttons`, with full pressure on each pressure-sensitive one, as a digital pad gives.
PadSample held(std::uint16_t buttons) {
    PadSample sample;
    sample.connected = true;
    sample.buttons = buttons;
    for (std::size_t i = 0; i < pad::kPressureCount; ++i) {
        if ((buttons & pad::kPressureButtons.at(i)) != 0) {
            sample.pressure.at(i) = 255;
        }
    }
    return sample;
}

// The pressure byte index of a pad::Pressure.
std::size_t at(pad::Pressure which) { return static_cast<std::size_t>(which); }

} // namespace

TEST_CASE("the stick table has a dead zone from 95 to 160 and reaches exactly -1 and 1", "[pad]") {
    CHECK(pad::stickValue(0) == -1.0F);
    CHECK(pad::stickValue(94) == -1.0F / 95.0F);
    CHECK(pad::stickValue(95) == 0.0F);
    CHECK(pad::stickValue(pad::kStickCentre) == 0.0F);
    CHECK(pad::stickValue(160) == 0.0F);
    CHECK(pad::stickValue(161) == 1.0F / 95.0F);
    CHECK(pad::stickValue(255) == 1.0F);
}

TEST_CASE("a pad reports pressed on the first sample, held while down and released on the first sample up", "[pad]") {
    Pad record;
    record.update(held(0));
    record.update(held(pad::kCross));
    CHECK(record.held(pad::kCross));
    CHECK(record.pressed() == pad::kCross);
    CHECK(record.pressed(pad::kCross));
    CHECK(record.released() == 0);

    record.update(held(pad::kCross));
    CHECK(record.held(pad::kCross));
    CHECK(record.pressed() == 0);

    record.update(held(0));
    CHECK_FALSE(record.held(pad::kCross));
    CHECK(record.released() == pad::kCross);
    CHECK(record.released(pad::kCross | pad::kCircle));
    CHECK_FALSE(record.released(pad::kCircle));
}

TEST_CASE("a pad keeps the last 8 button words and clamps older requests to the oldest", "[pad]") {
    Pad record;
    // Words 1, 2, ... 10: the ring then holds 3 to 10.
    for (std::uint16_t word = 1; word <= 10; ++word) {
        record.update(held(word));
    }
    CHECK(record.buttons() == 10);
    CHECK(record.buttons(1) == 9);
    CHECK(record.buttons(7) == 3);
    CHECK(record.buttons(8) == 3);
    CHECK(record.buttons(100) == 3);
}

TEST_CASE("a hold counter counts 1 to 15, then wraps to 12 so it is 15 every 4th sample", "[pad]") {
    Pad record;
    const std::uint8_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 12, 13, 14, 15, 12};
    for (const std::uint8_t count : expected) {
        record.update(held(pad::kDown));
        CHECK(record.holdCount(Pad::Direction::Down) == count);
        CHECK(record.holdCount(Pad::Direction::Up) == 0);
    }
    record.update(held(0));
    CHECK(record.holdCount(Pad::Direction::Down) == 0);
}

TEST_CASE("auto-repeat fires on the first, the 15th and every 4th held sample after", "[pad]") {
    Pad record;
    for (int sample = 1; sample <= 30; ++sample) {
        record.update(held(pad::kDown | pad::kStart));
        const bool fires = sample == 1 || sample == 15 || sample == 19 || sample == 23 || sample == 27;
        INFO("sample " << sample);
        CHECK(((record.pressedWithRepeat() & pad::kDown) != 0) == fires);
        // Only the d-pad repeats: START is pressed once.
        CHECK(((record.pressedWithRepeat() & pad::kStart) != 0) == (sample == 1));
    }
}

TEST_CASE("with two d-pad directions held only the more pressed one is kept", "[pad]") {
    Pad record;
    PadSample sample = held(pad::kUp | pad::kRight | pad::kCross);
    sample.pressure.at(at(pad::Pressure::Up)) = 200;
    sample.pressure.at(at(pad::Pressure::Right)) = 100;
    record.update(sample);
    CHECK(record.buttons() == (pad::kUp | pad::kCross));
    // The hold counters see the word as read, before the rule.
    CHECK(record.holdCount(Pad::Direction::Up) == 1);
    CHECK(record.holdCount(Pad::Direction::Right) == 1);

    // No pressure on either: neither is kept.
    sample.pressure = {};
    record.update(sample);
    CHECK(record.buttons() == pad::kCross);

    // Equal pressure: the first in pressure order (right, left, up, down) wins, a Coney choice.
    record.update(held(pad::kDown | pad::kLeft));
    CHECK(record.buttons() == pad::kLeft);

    // One direction is never filtered, whatever its pressure.
    sample = held(pad::kDown);
    sample.pressure = {};
    record.update(sample);
    CHECK(record.buttons() == pad::kDown);
}

TEST_CASE("the sticks are read from their raw bytes with y up", "[pad]") {
    Pad record;
    PadSample sample = held(0);
    sample.sticks = {255, 0, 0, 255}; // right stick right and up, left stick left and down
    record.update(sample);
    CHECK(record.rightX() == 1.0F);
    CHECK(record.rightY() == 1.0F);
    CHECK(record.leftX() == -1.0F);
    CHECK(record.leftY() == -1.0F);
    CHECK(record.rawSticks()[0] == 255);
}

TEST_CASE("a disconnected pad holds nothing and stops repeating", "[pad]") {
    Pad record;
    for (int i = 0; i < 15; ++i) {
        record.update(held(pad::kUp));
    }
    CHECK(record.holdCount(Pad::Direction::Up) == Pad::kRepeatCount);
    record.update(PadSample{});
    CHECK_FALSE(record.connected());
    CHECK(record.buttons() == 0);
    CHECK(record.released() == pad::kUp);
    CHECK(record.pressedWithRepeat() == 0);
    CHECK(record.leftX() == 0.0F);
}

TEST_CASE("the pads update records 0 and 4 for ports 1 and 2 and leave the others alone", "[pad]") {
    Pads pads;
    CHECK(Pads::recordOfPort(0) == 0);
    CHECK(Pads::recordOfPort(1) == 4);
    pads.update({held(pad::kStart), held(pad::kSelect)});
    CHECK(pads.port(0).pressed() == pad::kStart);
    CHECK(pads.record(4).pressed() == pad::kSelect);
    // The other six records: none connected, none holding anything.
    int untouched = 0;
    for (const std::size_t other : {1, 2, 3, 5, 6, 7}) {
        untouched += !pads.record(other).connected() && pads.record(other).buttons() == 0 ? 1 : 0;
    }
    CHECK(untouched == 6);
}
