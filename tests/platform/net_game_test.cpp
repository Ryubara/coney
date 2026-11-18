// SPDX-License-Identifier: GPL-3.0-or-later
// Network play's proof files in one run (docs/guides/multiplayer.md#digest): the refusals of NetGame::open(), the
// remote port replayed from a session record, the record's pad lines and the notes of network play's log.
#include "platform/net_game.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/options.h"
#include "core/pad.h"
#include "core/pads.h"
#include "net/session_record.h"
#include "support/fixtures.h"

using coney::ErrorCode;
using coney::InputSource;
using coney::Options;
using coney::PadSample;
using coney::PortSamples;
using coney::platform::NetGame;
using coney::test::TempDir;

namespace {

// The run's own pads: the same samples every step.
struct HeldPads final : InputSource {
    PortSamples samples{};
    PortSamples sample(std::uint64_t /*frame*/) override { return samples; }
};

// A connected pad with `buttons` held.
PadSample padWith(std::uint16_t buttons) {
    PadSample pad;
    pad.connected = true;
    pad.buttons = buttons;
    return pad;
}

// Writes `text` to `name` in `dir` and returns its path as a string, for an option.
std::string writeText(const TempDir& dir, std::string_view name, std::string_view text) {
    return dir.write(name, std::as_bytes(std::span(text.data(), text.size()))).string();
}

// The whole of the file at `path`.
std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("NetGame refuses a replay it cannot read or that is not a session record", "[net][net_game]") {
    // Expected: unverified, #558 (Coney's own proof files)
    TempDir dir;
    Options missing;
    missing.netReplayFile = (dir.path() / "absent.txt").string();
    auto refusedMissing = NetGame::open(missing);
    REQUIRE_FALSE(refusedMissing.has_value());
    CHECK(refusedMissing.error().code == ErrorCode::Io);

    Options malformed;
    malformed.netReplayFile = writeText(dir, "bad.txt", "pad 0 2\n");
    auto refusedMalformed = NetGame::open(malformed);
    REQUIRE_FALSE(refusedMalformed.has_value());
    CHECK(refusedMalformed.error().code == ErrorCode::Invalid);

    Options unwritable;
    unwritable.digestFile = (dir.path() / "no-such-folder" / "digest.txt").string();
    auto refusedDigest = NetGame::open(unwritable);
    REQUIRE_FALSE(refusedDigest.has_value());
    CHECK(refusedDigest.error().code == ErrorCode::Io);
}

TEST_CASE("NetGame replays the recorded pad on port 2 and leaves the rest to the run's pads", "[net][net_game]") {
    // Expected: unverified, #558 (a replay changes only the remote port)
    TempDir dir;
    const PadSample recorded = padWith(coney::pad::kCross);
    Options options;
    options.netReplayFile = writeText(dir, "record.txt", coney::net::formatPadLine(1, recorded) + "\n");
    auto opened = NetGame::open(options);
    REQUIRE(opened.has_value());
    NetGame& game = **opened;
    HeldPads pads;
    pads.samples.at(0) = padWith(coney::pad::kStart);
    pads.samples.at(1) = padWith(coney::pad::kSquare);
    game.setInner(&pads);

    // Step 0 is not in the record: the run's own pads.
    const PortSamples step0 = game.sample(0);
    CHECK(step0.at(0).buttons == coney::pad::kStart);
    CHECK(step0.at(1).buttons == coney::pad::kSquare);
    // Step 1 is: port 2 is the record's, port 1 still the run's.
    const PortSamples step1 = game.sample(1);
    CHECK(step1.at(0).buttons == coney::pad::kStart);
    CHECK(step1.at(1).connected);
    CHECK(step1.at(1).buttons == coney::pad::kCross);
}

TEST_CASE("NetGame records each step's remote pad and network play's notes", "[net][net_game]") {
    // Expected: unverified, #558 (the session record's lines)
    TempDir dir;
    const std::filesystem::path path = dir.path() / "record.txt";
    Options options;
    options.netRecordFile = path.string();
    HeldPads pads;
    pads.samples.at(1) = padWith(coney::pad::kCircle);
    {
        auto opened = NetGame::open(options);
        REQUIRE(opened.has_value());
        NetGame& game = **opened;
        game.setInner(&pads);
        (void)game.sample(0);
        // The log's prefix and newline go; an empty note or one of two lines is left out.
        game.note("net: guest joined\n");
        game.note("net: \n");
        game.note("two\nlines");
        (void)game.sample(1);
        game.note("guest left");
    } // closing the run flushes the record

    const std::string expected = coney::net::formatPadLine(0, pads.samples.at(1)) + "\n" +
                                 coney::net::formatEventLine(0, "guest joined") + "\n" +
                                 coney::net::formatPadLine(1, pads.samples.at(1)) + "\n" +
                                 coney::net::formatEventLine(1, "guest left") + "\n";
    CHECK(readText(path) == expected);
    // What it wrote is a record a replay reads back.
    auto replay = coney::net::SessionReplay::parse(readText(path));
    REQUIRE(replay.has_value());
    CHECK(replay->steps() == 2);
}
