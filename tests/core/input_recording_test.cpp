// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/input_recording.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"

using coney::InputRecorder;
using coney::PadSample;
using coney::PortSamples;
using coney::RecordingInput;
using coney::ScriptedInput;
namespace pad = coney::pad;

namespace {

// A source that plays back a fixed list of samples, one per step, and holds the last.
class ListInput final : public coney::InputSource {
  public:
    explicit ListInput(std::vector<PortSamples> samples) : m_samples(std::move(samples)) {}
    PortSamples sample(std::uint64_t frame) override {
        const std::size_t at = frame < m_samples.size() ? static_cast<std::size_t>(frame) : m_samples.size() - 1;
        return m_samples.at(at);
    }

  private:
    std::vector<PortSamples> m_samples;
};

// A pad at rest with port 1 plugged in, as a script starts.
PortSamples atRest() {
    PortSamples samples{};
    samples[0].connected = true;
    return samples;
}

// Sets the pressure bytes of the buttons held in `sample` to full, as a digital pad gives.
void fullPressure(PadSample& sample) {
    for (std::size_t i = 0; i < pad::kPressureCount; ++i) {
        sample.pressure.at(i) = (sample.buttons & pad::kPressureButtons.at(i)) != 0 ? 255 : 0;
    }
}

// A fixed pseudo-random run of `steps` samples: buttons going down and up, sticks anywhere on the byte range, a
// trigger's partial pressure, port 2 plugged in and out. A pad's other fields are as a real one gives them.
std::vector<PortSamples> wildRun(std::size_t steps) {
    std::uint32_t seed = 12345;
    const auto next = [&seed] {
        seed = seed * 1664525U + 1013904223U;
        return seed >> 8U;
    };
    std::vector<PortSamples> run;
    PortSamples now = atRest();
    for (std::size_t step = 0; step < steps; ++step) {
        for (std::size_t port = 0; port < coney::kPadPorts; ++port) {
            PadSample& sample = now.at(port);
            if (next() % 11 == 0) {
                sample.connected = !sample.connected;
            }
            if (!sample.connected) {
                continue;
            }
            if (next() % 4 == 0) {
                sample.buttons ^= static_cast<std::uint16_t>(1U << (next() % 16));
            }
            if (next() % 5 == 0) {
                sample.sticks.at(next() % 4) = static_cast<std::uint8_t>(next());
            }
            fullPressure(sample);
            if (next() % 6 == 0) {
                // A trigger pulled part of the way: L2's pressure is not the button's.
                sample.pressure.at(static_cast<std::size_t>(pad::Pressure::L2)) = static_cast<std::uint8_t>(next());
            }
        }
        run.push_back(now);
    }
    return run;
}

// Whether two samples agree in everything the game reads: a disconnected pad's other fields are ignored.
bool sameSample(const PadSample& a, const PadSample& b) {
    if (a.connected != b.connected) {
        return false;
    }
    return !a.connected || (a.buttons == b.buttons && a.sticks == b.sticks && a.pressure == b.pressure);
}

} // namespace

TEST_CASE("a recording names each change on its step, in the script's own words", "[input_recording]") {
    // Expected: Coney's own debug tooling, a recording is an input script (docs/guides/run-log.md#input-recording).
    InputRecorder recorder;
    // A pad that does not change is not written.
    CHECK(recorder.record(0, atRest()).empty());
    CHECK(recorder.record(1, atRest()).empty());
    PortSamples samples = atRest();
    samples[0].buttons = pad::kCross | pad::kStart;
    fullPressure(samples[0]);
    // 100 % up and 30 % left, as a script's values: written as `stick`.
    samples[0].sticks[2] = coney::stickByteFromPercent(-30);
    samples[0].sticks[3] = coney::stickByteFromPercent(-100);
    CHECK(recorder.record(7, samples) == "7 press cross start\n"
                                         "7 stick left -30 100\n");

    // Cross let go, port 2 plugged in with a raw stick byte no script value gives (a real pad's 101).
    samples[0].buttons = pad::kStart;
    fullPressure(samples[0]);
    samples[1].connected = true;
    samples[1].sticks[0] = 101;
    CHECK(recorder.record(9, samples) == "9 release cross\n"
                                         "9 p2 connect\n"
                                         "9 p2 rawstick right 101 128\n");
}

TEST_CASE("a trigger's partial pressure is a pressure line, and goes back to auto", "[input_recording]") {
    // Expected: Coney's own debug tooling, a recording is an input script (docs/guides/run-log.md#input-recording).
    InputRecorder recorder;
    PortSamples samples = atRest();
    samples[0].buttons = pad::kL2;
    samples[0].pressure.at(static_cast<std::size_t>(pad::Pressure::L2)) = 90;
    CHECK(recorder.record(3, samples) == "3 press l2\n"
                                         "3 pressure l2 90\n");
    // The same pressure again is nothing new.
    CHECK(recorder.record(4, samples).empty());
    // Full pressure with the button held is what the button's state gives: auto.
    samples[0].pressure.at(static_cast<std::size_t>(pad::Pressure::L2)) = 255;
    CHECK(recorder.record(5, samples) == "5 pressure l2 auto\n");
}

TEST_CASE("playing a recording back gives the samples it was made from, step by step", "[input_recording]") {
    // Expected: Coney's own debug tooling, a recording is an input script (docs/guides/run-log.md#input-recording).
    const std::vector<PortSamples> run = wildRun(400);
    InputRecorder recorder;
    std::string text;
    for (std::size_t step = 0; step < run.size(); ++step) {
        text += recorder.record(step, run.at(step));
    }
    auto events = coney::parseInputScript(text);
    REQUIRE(events.has_value());
    ScriptedInput replay(std::move(*events));
    for (std::size_t step = 0; step < run.size(); ++step) {
        const PortSamples played = replay.sample(step);
        for (std::size_t port = 0; port < coney::kPadPorts; ++port) {
            INFO("step " << step << " port " << port);
            CHECK(sameSample(played.at(port), run.at(step).at(port)));
        }
    }
}

TEST_CASE("a recording of a script replays to the script's own samples", "[input_recording]") {
    // Expected: Coney's own debug tooling, a recording is an input script (docs/guides/run-log.md#input-recording).
    auto events = coney::parseInputScript("2 tap start\n"
                                          "4 stick left 0 100\n"
                                          "6 press cross\n"
                                          "9 release cross\n"
                                          "9 stick left 0 0\n");
    REQUIRE(events.has_value());
    ScriptedInput script(*events);
    std::string text;
    {
        RecordingInput recording(script, [&text](std::string_view lines) { text += lines; });
        for (std::uint64_t frame = 0; frame < 12; ++frame) {
            (void)recording.sample(frame);
        }
        CHECK(recording.framesRecorded() == 12);
    }
    CHECK_THAT(text, Catch::Matchers::EndsWith("# frames 12\n"));
    auto recorded = coney::parseInputScript(text);
    REQUIRE(recorded.has_value());
    ScriptedInput replay(std::move(*recorded));
    ScriptedInput original(*events);
    for (std::uint64_t frame = 0; frame < 12; ++frame) {
        const PortSamples want = original.sample(frame);
        const PortSamples got = replay.sample(frame);
        CHECK(sameSample(got[0], want[0]));
    }
}

TEST_CASE("a recording that starts late counts its first step as 0 and leaves the earlier ones out",
          "[input_recording]") {
    // Expected: Coney's own debug tooling, a recording is an input script (docs/guides/run-log.md#input-recording).
    PortSamples held = atRest();
    held[0].buttons = pad::kCircle;
    fullPressure(held[0]);
    ListInput source({held});
    bool started = false;
    std::string text;
    RecordingInput recording(
        source, [&text](std::string_view lines) { text += lines; }, [&started] { return started; });
    (void)recording.sample(0);
    (void)recording.sample(1);
    CHECK(text.empty());
    CHECK(recording.framesRecorded() == 0);
    started = true;
    // What it records is handed on as it is.
    const PortSamples given = recording.sample(2);
    CHECK(given[0].buttons == pad::kCircle);
    CHECK(given[0].pressure == held[0].pressure);
    (void)recording.sample(3);
    CHECK(text == "0 press circle\n");
    CHECK(recording.framesRecorded() == 2);
}

TEST_CASE("a recording's header carries the seed, and --input-script reads it back", "[input_recording]") {
    // Expected: Coney's own debug tooling, a recording is an input script (docs/guides/run-log.md#input-recording).
    const coney::InputRecordingHeader header{.aiSeed = 42, .level = "level99", .checkpoint = 3};
    const std::string text = coney::inputRecordingHeader(header) + "5 tap cross\n";
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("# ai-seed 42\n"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("# play-level level99\n"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("# checkpoint 3\n"));
    CHECK(coney::inputScriptSeed(text) == std::optional<std::uint32_t>(42));
    // The header's comments do not stop the script from parsing.
    CHECK(coney::parseInputScript(text).has_value());
}

TEST_CASE("a header never carries a path or text in the level's place", "[input_recording]") {
    // Expected: Coney's own debug tooling, a recording is an input script (docs/guides/run-log.md#input-recording).
    for (const std::string_view odd : {"C:\\Users\\someone\\level", "/home/someone/level", "a level", ""}) {
        const std::string text =
            coney::inputRecordingHeader({.aiSeed = 0, .level = std::string(odd), .checkpoint = std::nullopt});
        CHECK_THAT(text, !Catch::Matchers::ContainsSubstring("play-level"));
        CHECK_THAT(text, !Catch::Matchers::ContainsSubstring("someone"));
    }
    CHECK_THAT(coney::inputRecordingHeader({.aiSeed = 0, .level = "sandbox:parkour", .checkpoint = std::nullopt}),
               Catch::Matchers::ContainsSubstring("# play-level sandbox:parkour\n"));
}

TEST_CASE("the seed is read from the leading comments only", "[input_recording]") {
    // Expected: Coney's own debug tooling, a recording is an input script (docs/guides/run-log.md#input-recording).
    CHECK(coney::inputScriptSeed("# ai-seed 7\n1 tap cross\n") == std::optional<std::uint32_t>(7));
    CHECK(coney::inputScriptSeed("# a note\n\n#ai-seed 2147483647\n") == std::optional<std::uint32_t>(2147483647U));
    // No header, a command first, a bad number, and a word that only starts like the key: none.
    CHECK_FALSE(coney::inputScriptSeed("1 tap cross\n# ai-seed 7\n").has_value());
    CHECK_FALSE(coney::inputScriptSeed("").has_value());
    CHECK_FALSE(coney::inputScriptSeed("# ai-seed many\n").has_value());
    CHECK_FALSE(coney::inputScriptSeed("# ai-seeded 7\n").has_value());
}

TEST_CASE("rawstick and pressure lines parse and are refused when malformed", "[input_recording]") {
    // Expected: Coney's own debug tooling, a recording is an input script (docs/guides/run-log.md#input-recording).
    auto events = coney::parseInputScript("1 rawstick left 0 255\n"
                                          "2 p2 rawstick right 128 128\n"
                                          "3 pressure l2 90\n"
                                          "4 pressure l2 auto\n");
    REQUIRE(events.has_value());
    REQUIRE(events->size() == 4);
    CHECK((*events)[0].action == coney::InputEvent::Action::RawStick);
    CHECK((*events)[0].x == 0);
    CHECK((*events)[0].y == 255);
    CHECK((*events)[1].port == 1);
    CHECK((*events)[1].stick == 1);
    CHECK((*events)[2].action == coney::InputEvent::Action::Pressure);
    CHECK((*events)[2].buttons == pad::kL2);
    CHECK((*events)[2].x == 90);
    CHECK((*events)[3].x == coney::kPressureAuto);

    for (const std::string_view bad :
         {"1 rawstick left 0\n", "1 rawstick up 0 0\n", "1 rawstick left 0 256\n", "1 rawstick left -1 0\n",
          "1 pressure start 1\n", "1 pressure cross\n", "1 pressure cross 256\n", "1 pressure cross loud\n"}) {
        INFO(bad);
        CHECK_FALSE(coney::parseInputScript(bad).has_value());
    }
}

TEST_CASE("a pressure line holds until auto, and a raw stick reaches the pad as written", "[input_recording]") {
    // Expected: Coney's own debug tooling, a recording is an input script (docs/guides/run-log.md#input-recording).
    auto events = coney::parseInputScript("1 press r2\n"
                                          "1 pressure r2 40\n"
                                          "1 rawstick left 101 3\n"
                                          "3 pressure r2 auto\n");
    REQUIRE(events.has_value());
    ScriptedInput script(std::move(*events));
    (void)script.sample(0);
    const PadSample first = script.sample(1)[0];
    CHECK(first.pressure.at(static_cast<std::size_t>(pad::Pressure::R2)) == 40);
    CHECK(first.sticks[2] == 101);
    CHECK(first.sticks[3] == 3);
    CHECK(script.sample(2)[0].pressure.at(static_cast<std::size_t>(pad::Pressure::R2)) == 40);
    CHECK(script.sample(3)[0].pressure.at(static_cast<std::size_t>(pad::Pressure::R2)) == 255);
}
