// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/deferred_input.h"

#include <cstdint>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"

namespace {

// A scripted pad that taps cross on its frame 0 and holds square from its frame 2.
coney::ScriptedInput crossThenSquare() {
    auto events = coney::parseInputScript("0 tap cross\n2 press square\n");
    REQUIRE(events.has_value());
    return coney::ScriptedInput(std::move(*events));
}

} // namespace

TEST_CASE("deferred input plays its script from the first ready frame, counted from 0", "[core][input]") {
    coney::ScriptedInput script = crossThenSquare();
    bool ready = false;
    coney::DeferredInput input(script, [&ready] { return ready; });

    // Before ready: port 1 plugged in and idle, port 2 unplugged, and nothing of the script applied.
    for (std::uint64_t frame = 0; frame < 5; ++frame) {
        const coney::PortSamples idle = input.sample(frame);
        CHECK(idle[0].connected);
        CHECK(idle[0].buttons == 0);
        CHECK_FALSE(idle[1].connected);
    }
    CHECK_FALSE(input.origin().has_value());
    CHECK(input.framesPlayed() == 0);

    // Ready at frame 5: that frame is the script's frame 0 (cross), frame 7 its frame 2 (square).
    ready = true;
    CHECK(input.sample(5)[0].buttons == coney::pad::kCross);
    CHECK(input.origin() == std::optional<std::uint64_t>(5));
    CHECK(input.sample(6)[0].buttons == 0);
    CHECK(input.sample(7)[0].buttons == coney::pad::kSquare);
    CHECK(input.framesPlayed() == 3);

    // Once started it keeps playing, whatever `ready` says later.
    ready = false;
    CHECK(input.sample(8)[0].buttons == coney::pad::kSquare);
}
