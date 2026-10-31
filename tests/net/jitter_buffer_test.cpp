// SPDX-License-Identifier: GPL-3.0-or-later
// The client's jitter buffer (docs/guides/client-world.md#jitter-buffer): the clock a delay behind the newest snapshot,
// the blend across the snapshots either side of it, easing back to the delay when snapshots come late or early,
// holding when starved and jumping when far off.
#include "net/jitter_buffer.h"

#include <cstdint>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "net/snapshot.h"

using Catch::Approx;
using namespace coney::net;

namespace {

constexpr std::uint64_t kStep = kTimeUnitsPerStep;

// A snapshot's humans that say which step it is: one human whose first word is the step.
SnapshotHumans frameOf(std::uint32_t step) {
    HumanWords words{};
    words.at(0) = step;
    return SnapshotHumans{{NetId{.slot = 0, .generation = 1}, words}};
}

// Pushes the snapshots of steps `first` to `last`, `every` steps apart.
void pushSteps(JitterBuffer& buffer, std::uint32_t first, std::uint32_t last, std::uint32_t every = 1) {
    for (std::uint32_t step = first; step <= last; step += every) {
        REQUIRE(buffer.push(step, frameOf(step)));
    }
}

} // namespace

// Expected: unverified, #555 (Coney's own; 3 steps is the architecture's 100 ms at 30 Hz)
TEST_CASE("jitter buffer: the clock starts the delay behind the newest and plays a step each client step",
          "[net][jitter]") {
    JitterBuffer buffer;
    buffer.advance(); // nothing held: nothing starts
    CHECK_FALSE(buffer.started());
    CHECK_FALSE(buffer.sample().has_value());

    pushSteps(buffer, 10, 13);
    buffer.advance();
    REQUIRE(buffer.started());
    CHECK(buffer.time() == 10 * kStep);
    CHECK(buffer.delay() == 3 * kStep);
    auto sample = buffer.sample();
    REQUIRE(sample.has_value());
    CHECK(sample->toStep == 10);
    CHECK(sample->alpha == 1.0F);
    REQUIRE(sample->to != nullptr);
    CHECK(sample->to->begin()->second.at(0) == 10);

    // A snapshot a step: the clock plays a whole step each time, on the delay, each sample a snapshot's own.
    for (std::uint32_t step = 14; step < 60; ++step) {
        REQUIRE(buffer.push(step, frameOf(step)));
        buffer.advance();
        CHECK(buffer.time() == (step - 3) * kStep);
        CHECK(buffer.speedPercent() == 100);
        CHECK(buffer.settled());
        sample = buffer.sample();
        REQUIRE(sample.has_value());
        CHECK(sample->fromStep == step - 4);
        CHECK(sample->toStep == step - 3);
        CHECK(sample->alpha == 1.0F);
        CHECK(sample->to->begin()->second.at(0) == step - 3);
    }
    CHECK(buffer.starvedSteps() == 0);
    CHECK(buffer.snaps() == 0);
    // Only what the clock still blends from is kept, and what is ahead of it.
    CHECK(buffer.size() <= 5);
    // A snapshot no newer than the newest is a caller's mistake, and changes nothing.
    CHECK_FALSE(buffer.push(59, frameOf(59)));
    CHECK_FALSE(buffer.push(20, frameOf(20)));
    CHECK(buffer.newestStep() == 59);
}

// Expected: unverified, #555 (Coney's own; snapshots at 15 Hz are two steps apart)
TEST_CASE("jitter buffer: between snapshots the sample blends by the time between them, across gaps", "[net][jitter]") {
    JitterBuffer buffer;
    pushSteps(buffer, 0, 6, 2); // 15 Hz
    buffer.advance();
    CHECK(buffer.time() == 3 * kStep);
    CHECK(buffer.intervalSteps() == 2);
    auto sample = buffer.sample();
    REQUIRE(sample.has_value());
    CHECK(sample->fromStep == 2);
    CHECK(sample->toStep == 4);
    CHECK(sample->alpha == Approx(0.5F));
    CHECK(sample->from->begin()->second.at(0) == 2);
    CHECK(sample->to->begin()->second.at(0) == 4);

    // A step with no snapshot is within the interval: the clock plays on at its speed.
    buffer.advance();
    CHECK(buffer.speedPercent() == 100);
    CHECK(buffer.time() == 4 * kStep);
    sample = buffer.sample();
    REQUIRE(sample.has_value());
    CHECK(sample->toStep == 4);
    CHECK(sample->alpha == 1.0F);

    // Snapshot 8 comes, 10 is lost, 12 comes: the clock blends from 8 to 12 across the gap.
    REQUIRE(buffer.push(8, frameOf(8)));
    buffer.advance(); // 5
    buffer.advance(); // 6
    REQUIRE(buffer.push(12, frameOf(12)));
    buffer.advance(); // 7
    buffer.advance(); // 8
    buffer.advance(); // 9
    sample = buffer.sample();
    REQUIRE(sample.has_value());
    CHECK(buffer.time() == 9 * kStep);
    CHECK(sample->fromStep == 8);
    CHECK(sample->toStep == 12);
    CHECK(sample->alpha == Approx(0.25F));
}

// Expected: unverified, #555 (Coney's own; the nudge is JitterConfig::nudgePercent, 5)
TEST_CASE("jitter buffer: late snapshots slow the clock and early ones speed it, back to the delay", "[net][jitter]") {
    JitterBuffer buffer;
    std::uint32_t newest = 3;
    pushSteps(buffer, 0, newest);
    buffer.advance();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(buffer.push(++newest, frameOf(newest)));
        buffer.advance();
    }
    REQUIRE(buffer.settled());

    // Late: three steps with no snapshot. The clock plays on into its delay, then slows, and never passes the newest.
    bool slowed = false;
    for (int i = 0; i < 3; ++i) {
        buffer.advance();
        slowed = slowed || buffer.speedPercent() == 95;
        CHECK(buffer.time() <= std::uint64_t{newest} * kStep);
    }
    CHECK(slowed);
    CHECK_FALSE(buffer.settled());
    // They come in a burst, then one a step: the clock eases back to the delay and plays whole steps again.
    for (int i = 0; i < 3; ++i) {
        REQUIRE(buffer.push(++newest, frameOf(newest)));
    }
    for (int i = 0; i < 80; ++i) {
        REQUIRE(buffer.push(++newest, frameOf(newest)));
        buffer.advance();
    }
    CHECK(buffer.settled());
    CHECK(buffer.speedPercent() == 100);
    CHECK(buffer.delay() == 3 * kStep);
    CHECK(buffer.time() % kStep == 0);

    // Early: two a step for a while (a server running ahead, or a backlog draining): the clock speeds up.
    bool hurried = false;
    for (int i = 0; i < 4; ++i) {
        REQUIRE(buffer.push(++newest, frameOf(newest)));
        REQUIRE(buffer.push(++newest, frameOf(newest)));
        buffer.advance();
        hurried = hurried || buffer.speedPercent() == 105;
    }
    CHECK(hurried);
    for (int i = 0; i < 120; ++i) {
        REQUIRE(buffer.push(++newest, frameOf(newest)));
        buffer.advance();
    }
    CHECK(buffer.settled());
    CHECK(buffer.delay() == 3 * kStep);
    CHECK(buffer.time() % kStep == 0);
    CHECK(buffer.snaps() == 0);
}

// Expected: unverified, #555 (Coney's own; JitterConfig::snapSteps is 15)
TEST_CASE("jitter buffer: a starved clock holds at the newest snapshot, and a far one jumps to its target",
          "[net][jitter]") {
    JitterBuffer buffer;
    pushSteps(buffer, 0, 3);
    buffer.advance();
    for (int i = 0; i < 10; ++i) {
        buffer.advance();
        CHECK(buffer.time() <= 3 * kStep);
    }
    CHECK(buffer.time() == 3 * kStep);
    CHECK(buffer.starvedSteps() > 0);
    auto sample = buffer.sample();
    REQUIRE(sample.has_value());
    CHECK(sample->toStep == 3);
    CHECK(sample->alpha == 1.0F);

    // A snapshot far ahead (the server stalled, or skipped time): the clock jumps rather than easing for seconds.
    REQUIRE(buffer.push(40, frameOf(40)));
    buffer.advance();
    CHECK(buffer.snaps() == 1);
    CHECK(buffer.time() == 37 * kStep);
    sample = buffer.sample();
    REQUIRE(sample.has_value());
    CHECK(sample->toStep == 40);
}
