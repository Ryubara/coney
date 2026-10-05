// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/mission_complete_mode.h"

#include <format>
#include <utility>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_flow_mode.h"

namespace coney {

MissionCompleteMode::MissionCompleteMode(graphics::RenderDevice& device, GameModeStack& stack, LevelFlowMode& levelFlow,
                                         script::ScriptSystem& scripts, GameState& state,
                                         std::function<void(std::string_view)> log)
    : m_device(device), m_stack(stack), m_levelFlow(levelFlow), m_scripts(scripts), m_state(state),
      m_log(std::move(log)) {}

void MissionCompleteMode::launch(int kind) {
    m_kind = kind;
    ++m_launches;
    const bool push = m_stack.topId() != kId;
    m_log(std::format("mission complete: kind {}{}\n", kind, push ? "" : " (already on top)"));
    if (push) {
        m_stack.push(*this);
    }
}

void MissionCompleteMode::enter() {
    // The live humans would be put into a still state here; at the front end there are none, and Coney's play mode
    // keeps its own player. Then the unlocks and the next mission, which chooses the level (runNextMission(1)).
    m_scripts.call(kUnlockAndLoad);
}

ModeResult MissionCompleteMode::update(GameModeStack& stack, const FrameTime& frame) {
    // One frame of the world: here only the scripts' step, timed by the game time.
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    m_scripts.setTime(nowMs);
    m_scripts.update(nowMs, frame.seconds);
    if (m_kind == 0) {
        return ModeResult::Stay;
    }

    // The kind is set: leave, then act on it. exit() clears the kind, so it is read first.
    const int kind = m_kind;
    stack.pop();
    switch (kind) {
    case kKindCheckpointOne:
        m_state.checkPoint = 1;
        break;
    case kKindReloadLevel:
        m_levelFlow.chooseLevelIndex(m_state.currentLevel);
        break;
    case kKindNextLevel:
        m_levelFlow.chooseLevelIndex(m_state.currentLevel + 1);
        break;
    default:
        break;
    }
    // Gameplay below goes too, so the level flow on top starts the chosen level.
    if (stack.topId() == GameplayMode::kId) {
        stack.pop();
    }
    return ModeResult::Stay;
}

void MissionCompleteMode::render(const RenderTime& /*time*/) {
    m_device.beginFrame(graphics::kBlack);
    m_device.present();
}

} // namespace coney
