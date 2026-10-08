// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A scripted pad a disc test can steer while it runs: a player who reads the screen (a meter's target, who stands
// where) and presses accordingly. No game data.

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/input_script.h"
#include "core/pads.h"

namespace coney::test {

/// An input source playing one input script at a time; play() swaps in another from the next frame on.
class LivePad final : public InputSource {
  public:
    /// Plays `events` until play() is called.
    explicit LivePad(std::vector<InputEvent> events = {})
        : m_current(std::make_unique<ScriptedInput>(std::move(events))) {}

    /// From the next frame on, `text` plays (its frames are absolute).
    void play(std::string_view text) {
        auto events = parseInputScript(text);
        REQUIRE(events.has_value());
        if (events) {
            m_current = std::make_unique<ScriptedInput>(std::move(*events));
        }
    }

    /// The next frame the loop will ask for.
    [[nodiscard]] std::uint64_t nextFrame() const { return m_next; }

    /// The current script's sample for `frame`.
    PortSamples sample(std::uint64_t frame) override {
        m_next = frame + 1;
        return m_current->sample(frame);
    }

  private:
    std::unique_ptr<ScriptedInput> m_current;
    std::uint64_t m_next = 0;
};

} // namespace coney::test
