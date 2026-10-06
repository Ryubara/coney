// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/rumble_result_mode.h"

#include <format>
#include <utility>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"

namespace coney {

gui::PauseOutcome rumbleResultOutcome(gui::RumbleResultChoice choice, bool fromFrontEnd) {
    using gui::PauseOutcome;
    switch (choice) {
    case gui::RumbleResultChoice::Replay:
        return PauseOutcome::RestartLevel;
    case gui::RumbleResultChoice::RumbleMenu:
        return fromFrontEnd ? PauseOutcome::QuitToRumbleQuick : PauseOutcome::QuitToRumbleHangout;
    case gui::RumbleResultChoice::Quit:
        break;
    }
    return fromFrontEnd ? PauseOutcome::QuitToMainMenu : PauseOutcome::QuitToHangout;
}

RumbleResultMode::RumbleResultMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet,
                                   const gui::GlobalStrings& strings, FrontEndServices& services, GameState& state,
                                   LevelFlowMode& levelFlow, GameModeStack& stack, std::function<bool()> fromFrontEnd,
                                   std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_strings(strings), m_services(services), m_state(state),
      m_levelFlow(levelFlow), m_stack(stack), m_fromFrontEnd(std::move(fromFrontEnd)), m_log(std::move(log)) {
    m_menu.setStrings(&m_strings);
    m_menu.setSoundSink([this](int cue) { m_services.playCue(cue); });
}

void RumbleResultMode::launch(std::string_view winner, std::string_view reason) {
    m_winner = winner;
    m_reason = reason;
    m_log(std::format("rumble result: winner \"{}\", reason \"{}\"\n", winner, reason));
    if (m_stack.topId() != kId) {
        m_stack.push(*this);
    }
}

void RumbleResultMode::enter() {
    // Only the fonts: the screen has no sprite sheet of its own.
    m_layer.load(m_loadSheet, PauseRecordLoader{}, std::nullopt, 0, 0.0F, m_log, "rumble result");
    m_openPending = true;
}

ModeResult RumbleResultMode::update(GameModeStack& stack, const FrameTime& frame) {
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    // The world goes on under the screen: the win camera circles, the scripts run.
    if (m_world != nullptr) {
        (void)m_world->updateWorld(stack, frame);
    }
    if (m_openPending) {
        m_openPending = false;
        m_menu.open(m_winner, m_reason, m_fromFrontEnd && m_fromFrontEnd(), nowMs);
    }
    m_layer.begin();
    m_menu.update(gui::GuiFrame{nowMs, &stack.pads().port(0)});
    m_menu.render(m_layer.canvas());
    m_layer.queue();

    // A choice ends the screen: leave, then act on it as the pause menu would.
    if (const std::optional<gui::RumbleResultChoice> choice = m_menu.choice()) {
        const bool fromFrontEnd = m_fromFrontEnd && m_fromFrontEnd();
        m_log(std::format("rumble result: choice {}\n", static_cast<int>(*choice)));
        stack.pop();
        applyPauseOutcome(rumbleResultOutcome(*choice, fromFrontEnd), stack, m_state, m_levelFlow, m_services, m_log);
    }
    return ModeResult::Stay;
}

void RumbleResultMode::render(const RenderTime& time) {
    const std::function<void(graphics::RenderDevice&)> overlay = [this](graphics::RenderDevice& device) {
        m_layer.draw(device);
    };
    if (m_world != nullptr) {
        m_world->renderWithOverlay(time, overlay);
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    overlay(m_device);
    m_device.present();
}

void RumbleResultMode::exit() {
    m_menu.close();
    m_layer.release();
}

} // namespace coney
