// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/input_script.h"
#include "core/pads.h"

// Plays an --input-script through the pad records, as the game would, so the combat tests are fed the same samples a
// scripted run gives: the button word and the left stick (partial deflections included, dead zone applied).

namespace coney::test {

/// One update's pad 1 as combat sees it.
struct PadFrame {
    std::uint16_t buttons = 0;
    float leftX = 0.0F; ///< Right positive.
    float leftY = 0.0F; ///< Up positive.
};

/// The first `frames` updates of the input script `text` on port 1.
inline std::vector<PadFrame> playScript(std::string_view text, std::uint64_t frames) {
    auto events = parseInputScript(text);
    REQUIRE(events.has_value());
    ScriptedInput input(std::move(events).value_or(std::vector<InputEvent>{}));
    Pads pads;
    std::vector<PadFrame> out;
    out.reserve(frames);
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        pads.update(input.sample(frame));
        const Pad& pad = pads.port(0);
        out.push_back(PadFrame{pad.buttons(), pad.leftX(), pad.leftY()});
    }
    return out;
}

} // namespace coney::test
