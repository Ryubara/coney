// SPDX-License-Identifier: GPL-3.0-or-later
// The server's buffer of a player's input commands (docs/guides/multiplayer.md#input-commands): the delay at the
// start, one step played a server step, late and repeated steps, a missing step, a stall and the skip back to the
// delay, and letting a quiet pad go.
#include "net/input_buffer.h"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"

using coney::net::InputBuffer;
using coney::net::InputBufferConfig;

namespace {

// A plugged-in pad with `buttons` held.
coney::PadSample held(std::uint16_t buttons) {
    coney::PadSample pad;
    pad.connected = true;
    pad.buttons = buttons;
    return pad;
}

} // namespace

TEST_CASE("an input buffer reads unplugged until the first step it plays", "[net][input]") {
    InputBuffer buffer(InputBufferConfig{.delaySteps = 2});
    CHECK_FALSE(buffer.take().connected); // nothing has come
    CHECK(buffer.lead() == 0);
    buffer.add(0, held(coney::pad::kCross));
    CHECK(buffer.lead() == 3); // steps -2 and -1, the delay filling, then 0
    CHECK_FALSE(buffer.take().connected);
    CHECK_FALSE(buffer.take().connected);
    CHECK(buffer.take().buttons == coney::pad::kCross);
    CHECK(buffer.stats().applied == 1);
    CHECK(buffer.stats().missed == 0); // the delay filling is not a missed step
}

TEST_CASE("an input buffer plays one step a take, in step order, each once", "[net][input]") {
    InputBuffer buffer(InputBufferConfig{.delaySteps = 1});
    buffer.add(0, held(coney::pad::kSquare));
    (void)buffer.take();
    buffer.add(2, held(coney::pad::kTriangle)); // out of order
    buffer.add(1, held(coney::pad::kCircle));
    buffer.add(1, held(coney::pad::kCross)); // a repeat: the first copy stands
    CHECK(buffer.take().buttons == coney::pad::kSquare);
    CHECK(buffer.take().buttons == coney::pad::kCircle);
    CHECK(buffer.current().buttons == coney::pad::kCircle);
    buffer.add(1, held(coney::pad::kR1)); // already played
    CHECK(buffer.take().buttons == coney::pad::kTriangle);
    CHECK(buffer.stats().applied == 3);
}

TEST_CASE("a step that never comes plays the step before's pad", "[net][input]") {
    InputBuffer buffer(InputBufferConfig{.delaySteps = 1});
    buffer.add(0, held(coney::pad::kL1));
    buffer.add(2, held(coney::pad::kR1)); // step 1 lost even from the packets repeating it
    (void)buffer.take();
    CHECK(buffer.take().buttons == coney::pad::kL1);
    CHECK(buffer.take().buttons == coney::pad::kL1); // step 1: missing
    CHECK(buffer.take().buttons == coney::pad::kR1);
    CHECK(buffer.stats().missed == 1);
}

TEST_CASE("a stall holds the last pad and the delay grows, up to its slack", "[net][input]") {
    InputBuffer buffer(InputBufferConfig{.delaySteps = 1, .slackSteps = 2, .holdSteps = 100});
    std::uint32_t step = 0;
    buffer.add(step, held(coney::pad::kCross));
    (void)buffer.take();
    for (int i = 0; i < 3; ++i) {
        buffer.add(++step, held(coney::pad::kCross));
        (void)buffer.take();
    }
    CHECK(buffer.lead() == 1);

    // The client's next three steps are lost: the pad holds, nothing is missed, and play falls behind.
    for (int i = 0; i < 3; ++i) {
        CHECK(buffer.take().buttons == coney::pad::kCross);
    }
    step += 3;
    CHECK(buffer.stats().stalled == 2); // the first was covered by the delay
    CHECK(buffer.stats().missed == 0);

    // Packets flow again, each with the three steps before it: the lost steps play, two steps late, which is within
    // the slack.
    for (int i = 0; i < 3; ++i) {
        ++step;
        for (std::uint32_t back = 0; back <= 3; ++back) {
            buffer.add(step - back, held(coney::pad::kCircle));
        }
        (void)buffer.take();
    }
    CHECK(buffer.lead() == 3);
    CHECK(buffer.stats().missed == 0);
    CHECK(buffer.stats().skipped == 0);
}

TEST_CASE("an input buffer too far behind skips back to its delay", "[net][input]") {
    InputBuffer buffer(InputBufferConfig{.delaySteps = 1, .slackSteps = 2});
    buffer.add(0, held(coney::pad::kCross));
    (void)buffer.take();
    // A burst: steps 1 to 6 at once, 6 ahead of step 0, the one next played: over the delay and slack of 3.
    for (std::uint32_t step = 1; step <= 4; ++step) {
        buffer.add(step, held(coney::pad::kCross));
    }
    buffer.add(5, held(coney::pad::kR2));
    buffer.add(6, held(coney::pad::kL2));
    CHECK(buffer.take().buttons == coney::pad::kR2); // steps 0 to 4 dropped: step 5 is the delay's one behind 6
    CHECK(buffer.stats().skipped == 5);
    CHECK(buffer.lead() == 1);
    CHECK(buffer.take().buttons == coney::pad::kL2);
}

TEST_CASE("a pad with nothing new for longer than holdSteps is let go", "[net][input]") {
    InputBuffer buffer(InputBufferConfig{.delaySteps = 0, .slackSteps = 2, .holdSteps = 3});
    coney::PadSample pad = held(coney::pad::kR1);
    pad.sticks.at(2) = 255;
    buffer.add(0, pad);
    CHECK(buffer.take().buttons == coney::pad::kR1);
    for (int i = 0; i < 3; ++i) {
        CHECK(buffer.take().buttons == coney::pad::kR1); // held through a gap of up to holdSteps
    }
    const coney::PadSample rest = buffer.take();
    CHECK(rest.connected);
    CHECK(rest.buttons == 0);
    CHECK(rest.sticks == coney::PadSample{}.sticks);
    buffer.add(1, held(coney::pad::kCross)); // the next step takes over again
    CHECK(buffer.take().buttons == coney::pad::kCross);
}
