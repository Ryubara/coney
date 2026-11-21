// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/mission_failed_mode.h"

#include <format>
#include <optional>
#include <utility>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"

namespace coney {

MissionFailedMode::MissionFailedMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet,
                                     PauseRecordLoader loadRecord, const gui::GlobalStrings& strings,
                                     FrontEndServices& services, GameState& state, LevelFlowMode& levelFlow,
                                     GameModeStack& stack, std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_loadRecord(std::move(loadRecord)), m_strings(strings),
      m_services(services), m_state(state), m_levelFlow(levelFlow), m_stack(stack), m_log(std::move(log)) {
    m_menu.setStrings(&m_strings);
    m_menu.setSoundSink([this](int cue) { m_services.playCue(cue); });
}

void MissionFailedMode::launch(std::string_view reason) {
    m_reason = reason;
    m_log(std::format("mission failed: reason of {} bytes\n", reason.size()));
    if (m_stack.topId() != kId) {
        m_stack.push(*this);
    }
}

void MissionFailedMode::enter() {
    if (m_hooks.pauseSound) {
        m_hooks.pauseSound(true);
    }
    m_layer.load(m_loadSheet, m_loadRecord, gui::MissionFailedMenu::titleRecord(m_state.language), kTitleCapacity,
                 gui::MissionFailedMenu::kTitleDepth, m_log, "mission failed");
    m_menu.setTitleBatch(m_layer.sprites());
    m_fadeLevel.reset(0.0F);
    m_openPending = true;
}

ModeResult MissionFailedMode::update(GameModeStack& stack, const FrameTime& frame) {
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    // The menu and the fade start on the first frame, which knows the time.
    if (m_openPending) {
        m_openPending = false;
        m_frozen = RenderTime{.alpha = 1.0F, .gameTicks = frame.gameTicks - frame.stepTicks, .index = 0};
        int level = 0;
        if (const LevelRecord* record = m_state.levels.at(m_state.currentLevel)) {
            level = static_cast<int>(record->number);
        }
        m_fade.queue(graphics::ScreenFade::kFadeOut, gui::MissionFailedMenu::kFadeSeconds, nowMs);
        m_menu.open(level, m_reason, nowMs);
    }

    m_fadeLevel.commit();
    m_fade.update(nowMs);
    m_fadeLevel.current() = m_fade.level();
    m_layer.begin();
    m_menu.update(gui::GuiFrame{nowMs, &stack.pads().port(0)});
    m_menu.render(m_layer.canvas());
    m_layer.queue();

    // A choice ends the menu: leave, then act on it.
    if (const std::optional<gui::PauseOutcome> outcome = m_menu.outcome()) {
        stack.pop();
        applyPauseOutcome(*outcome, stack, m_state, m_levelFlow, m_services, m_log);
    }
    return ModeResult::Stay;
}

void MissionFailedMode::render(const RenderTime& time) {
    const float level = lerp(m_fadeLevel.previous(), m_fadeLevel.current(), time.alpha);
    const std::function<void(graphics::RenderDevice&)> overlay = [this, level](graphics::RenderDevice& device) {
        graphics::ScreenFade::draw(device, level);
        m_layer.draw(device);
    };
    if (m_world != nullptr) {
        m_world->renderWithOverlay(m_frozen, overlay);
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    overlay(m_device);
    m_device.present();
}

void MissionFailedMode::exit() {
    if (m_hooks.pauseSound) {
        m_hooks.pauseSound(false);
    }
    m_menu.setTitleBatch(nullptr);
    m_layer.release();
}

} // namespace coney
