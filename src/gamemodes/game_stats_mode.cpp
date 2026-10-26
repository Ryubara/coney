// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/game_stats_mode.h"

#include <format>
#include <utility>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gui/text_layout.h"

namespace coney {

GameStatsMode::GameStatsMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet, PauseRecordLoader loadRecord,
                             const gui::GlobalStrings& strings, FrontEndServices& services, graphics::ScreenFade& fade,
                             GameState& state, script::ScriptSystem& scripts, GameModeStack& stack,
                             std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_loadRecord(std::move(loadRecord)), m_strings(strings),
      m_services(services), m_fade(fade), m_state(state), m_scripts(scripts), m_stack(stack), m_log(std::move(log)) {}

void GameStatsMode::launch(std::string_view onClose) {}

void GameStatsMode::enter() {}

ModeResult GameStatsMode::update(GameModeStack& stack, const FrameTime& frame) { return {}; }

void GameStatsMode::render(const RenderTime& /*time*/) {}

void GameStatsMode::exit() {}

} // namespace coney
