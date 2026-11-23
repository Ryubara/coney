// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that an input recording replays to the same state (docs/guides/run-log.md#input-
// recording): `--play-level level99` played headless for 150 steps from a pad that gives what a real one does (raw
// stick bytes no script percentage reaches, a trigger pulled part of the way, buttons going down and up), recorded
// through RecordingInput; the recording, parsed and played back as `--input-script` plays it, must leave player 1
// where, and as, the first run did. It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/game_timer.h"
#include "core/input_recording.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "support/disc_play_fixtures.h"
#include "world/sector_budget.h"

namespace {

// Steps each run plays: five seconds at 30 a second.
constexpr std::uint64_t kSteps = 150;

// A pad as a person's would be: deterministic, but nothing a script percentage gives. The left stick sweeps through
// raw bytes (101, 102, ... on the way round), the right stick wiggles, L2 is pulled to a partial pressure for a while,
// and cross and square go down and up.
class WigglePad final : public coney::InputSource {
  public:
    coney::PortSamples sample(std::uint64_t frame) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        // The left stick pushed up most of the way (0 is up), with an x that drifts through bytes on both sides of the
        // dead zone.
        pad.sticks[2] = static_cast<std::uint8_t>(40 + (frame * 5) % 80);
        pad.sticks[3] = static_cast<std::uint8_t>(20 + (frame * 3) % 40);
        pad.sticks[0] = static_cast<std::uint8_t>(100 + (frame * 7) % 56);
        if (frame % 40 >= 10 && frame % 40 < 20) {
            pad.buttons |= coney::pad::kCross;
        }
        if (frame % 55 >= 30) {
            pad.buttons |= coney::pad::kSquare;
        }
        if (frame >= 60 && frame < 90) {
            pad.buttons |= coney::pad::kL2;
        }
        for (std::size_t i = 0; i < coney::pad::kPressureCount; ++i) {
            pad.pressure.at(i) = (pad.buttons & coney::pad::kPressureButtons.at(i)) != 0 ? 255 : 0;
        }
        if ((pad.buttons & coney::pad::kL2) != 0) {
            pad.pressure.at(static_cast<std::size_t>(coney::pad::Pressure::L2)) = static_cast<std::uint8_t>(80 + frame);
        }
        return samples;
    }
};

// What a run leaves of player 1: where he stands, how far he has gone, how he is.
struct Outcome {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float travelled = 0.0F;
    int health = 0;
    bool operator==(const Outcome&) const = default;
};

// Plays level99 headless for kSteps steps with `input` and reports player 1's state.
Outcome play(const coney::io::Wad& wad, coney::InputSource& input) {
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    auto mode = coney::platform::PlayLevelMode::create(**engine, wad, "level99", budget, [](std::string_view) {});
    REQUIRE(mode.has_value());
    coney::GameModeStack stack;
    stack.push(**mode);
    stack.setInput(&input);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    CHECK(stack.runUntilEmpty(timer, {}, kSteps) == kSteps);
    const coney::anim::Vec3 feet = (*mode)->playerFeet();
    return Outcome{.x = feet.x,
                   .y = feet.y,
                   .z = feet.z,
                   .travelled = (*mode)->stats().travelled,
                   .health = static_cast<int>((*mode)->player().human().health().value())};
}

} // namespace

TEST_CASE("the disc's level99: a recorded run, played back as an input script, ends in the same state",
          "[disc][play][input_recording]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // The first run: a person's pad, recorded.
    std::string text = coney::inputRecordingHeader({.aiSeed = 9, .level = "level99", .checkpoint = std::nullopt});
    Outcome first;
    {
        WigglePad pad;
        coney::RecordingInput recording(pad, [&text](std::string_view lines) { text += lines; });
        first = play(*wad, recording);
        CHECK(recording.framesRecorded() == kSteps);
    }

    // Expected: docs/guides/run-log.md#input-recording (Coney's own format: the script lines of input_script.h; the
    // header's seed is what --input-script hands the session).
    CHECK(coney::inputScriptSeed(text) == std::optional<std::uint32_t>(9));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("rawstick left"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("pressure l2 "));
    CHECK_THAT(text, Catch::Matchers::EndsWith("# frames 150\n"));
    CHECK((first.travelled > 1.0F));

    // The second run: the recording, as --input-script reads it.
    auto events = coney::parseInputScript(text);
    REQUIRE(events.has_value());
    coney::ScriptedInput replay(std::move(*events));
    const Outcome second = play(*wad, replay);
    CHECK(second == first);
}
