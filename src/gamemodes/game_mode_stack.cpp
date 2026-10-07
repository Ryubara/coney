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
    if (leaving == m_lastUpdated) {
        m_lastUpdated = nullptr;
    }
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

void GameModeStack::updateTop(const FrameTime& frame) {
    GameMode* mode = top();
    if (mode == nullptr) {
        return;
    }
    if (!mode->entered()) {
        // Marked entered before enter() runs, so a mode that pushes another from enter() is suspended properly.
        mode->m_entered = true;
        mode->enter();
    }
    m_lastUpdated = mode;
    m_lastStep = frame;
    m_popPending = mode->update(*this, frame) == ModeResult::Leave;
}

void GameModeStack::finishPop() {
    if (m_popPending) {
        m_popPending = false;
        pop();
    }
}

void GameModeStack::step(const FrameTime& frame) {
    updateTop(frame);
    finishPop();
}

void GameModeStack::render(const RenderTime& time) {
    GameMode* mode = m_lastUpdated;
    if (mode == nullptr) {
        GameMode* candidate = top();
        mode = candidate != nullptr && candidate->entered() ? candidate : nullptr;
    }
    if (mode != nullptr) {
        mode->render(time);
        ++m_renders;
    }
}

LoopCounts GameModeStack::runUntilEmpty(GameTimer& timer, FrameClock& clock, const FrameHooks& hooks,
                                        std::optional<std::uint64_t> stepLimit) {
    LoopCounts counts;
    const auto belowLimit = [&counts, stepLimit] { return !stepLimit || counts.steps + counts.held < *stepLimit; };
    while (!empty() && belowLimit()) {
        // Wait out the frame cap first and then read the events, so the steps see the freshest input.
        const std::uint64_t elapsed = hooks.waitForFrame ? hooks.waitForFrame() : 0;
        if (hooks.beginFrame && !hooks.beginFrame()) {
            break;
        }
        const FramePlan plan = clock.advance(elapsed);

        // The steps the real time calls for. A Leave from the step before is carried out first.
        std::uint32_t ran = 0;
        for (std::uint32_t due = 0; due < plan.steps && belowLimit(); ++due) {
            finishPop();
            if (empty()) {
                break;
            }
            // The pads are read once per step, after the window's events; a held step reads them too, for the menus.
            samplePads(m_samples++);
            if (m_stepGate && !m_stepGate()) {
                m_holding = true;
                ++counts.held;
                continue;
            }
            ++ran;
            m_holding = false;
            // A mode that stops the game clock (a movie) still steps 1/30 s of its own time, but game time stays.
            const bool clockStill = top()->stopsGameClock();
            const std::uint64_t advanced = clockStill ? 0 : timer.update();
            const double seconds = GameTimer::toSeconds(clockStill ? GameTimer::kFixedStepTicks : advanced);
            updateTop(FrameTime{m_steps, seconds, timer.ticks(), advanced});
            ++m_steps;
            ++counts.steps;
        }

        // One render between the last two steps, by alpha (exactly the newest step at alpha 1); then the last step's
        // Leave, if any. While the gate holds the steps, the newest step shows as it is, so a paused game stands still.
        const float alpha = m_holding ? 1.0F : plan.alpha;
        const auto behind =
            static_cast<std::uint64_t>((1.0 - static_cast<double>(alpha)) * static_cast<double>(m_lastStep.stepTicks));
        render(RenderTime{.alpha = alpha, .gameTicks = m_lastStep.gameTicks - behind, .index = m_renders});
        finishPop();
        ++counts.frames;
        if (hooks.endFrame) {
            hooks.endFrame(ran);
        }
    }
    finishPop();
    return counts;
}

std::uint64_t GameModeStack::runUntilEmpty(GameTimer& timer, const std::function<bool()>& beginFrame,
                                           std::optional<std::uint64_t> stepLimit) {
    FrameClock lockstep(FramePacing::Lockstep);
    FrameHooks hooks;
    hooks.beginFrame = beginFrame;
    return runUntilEmpty(timer, lockstep, hooks, stepLimit).steps;
}

void GameModeStack::samplePads(std::uint64_t frame) {
    if (m_input != nullptr) {
        m_pads.update(m_input->sample(frame));
    }
}

} // namespace coney
