// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/game_mode_stack.h"

#include <algorithm>

#include "core/assert.h"

namespace coney {

void GameModeStack::push(GameMode& mode) {
    CONEY_ASSERT(std::ranges::find(m_modes, &mode) == m_modes.end());
    if (GameMode* below = top(); below != nullptr && below->entered()) {
        below->suspend();
    }
    m_modes.push_back(&mode);
}

void GameModeStack::pop() {
    CONEY_ASSERT(!m_modes.empty());
    GameMode* leaving = m_modes.back();
    m_modes.pop_back();
    if (leaving->entered()) {
        leaving->exit();
        leaving->m_entered = false;
    }
    if (GameMode* below = top(); below != nullptr && below->entered()) {
        below->resume();
    }
}

GameMode* GameModeStack::top() const { return m_modes.empty() ? nullptr : m_modes.back(); }

std::uint32_t GameModeStack::topId() const { return m_modes.empty() ? 0 : m_modes.back()->id(); }

void GameModeStack::step(const FrameTime& frame) {
    GameMode* mode = top();
    if (mode == nullptr) {
        return;
    }
    if (!mode->entered()) {
        // Marked entered before enter() runs, so a mode that pushes another from enter() is suspended properly.
        mode->m_entered = true;
        mode->enter();
    }
    if (mode->update(*this, frame) == ModeResult::Leave) {
        pop();
    }
}

std::uint64_t GameModeStack::runUntilEmpty(GameTimer& timer, const std::function<bool()>& beginFrame,
                                           std::optional<std::uint64_t> frameLimit) {
    std::uint64_t frames = 0;
    while (!empty() && (!frameLimit || frames < *frameLimit)) {
        if (beginFrame && !beginFrame()) {
            break;
        }
        // The pads are read once per frame, after the window's events (which carry the keyboard and gamepad state).
        samplePads(frames);
        const std::uint64_t advanced = timer.update();
        step(FrameTime{frames, GameTimer::toSeconds(advanced), timer.ticks(), advanced});
        ++frames;
    }
    return frames;
}

void GameModeStack::samplePads(std::uint64_t frame) {
    if (m_input != nullptr) {
        m_pads.update(m_input->sample(frame));
    }
}

} // namespace coney
