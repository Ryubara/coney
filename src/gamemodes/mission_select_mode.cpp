// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/mission_select_mode.h"

#include <utility>

#include "gamemodes/game_mode_stack.h"

namespace coney {

MissionSelectMode::MissionSelectMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet,
                                     PauseRecordLoader loadRecord, const gui::GlobalStrings& strings,
                                     FrontEndServices& services, graphics::ScreenFade& fade, GameState& state,
                                     script::ScriptSystem& scripts, GameModeStack& stack, bool /*europe*/,
                                     std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_loadRecord(std::move(loadRecord)), m_strings(strings),
      m_services(services), m_fade(fade), m_state(state), m_scripts(scripts), m_stack(stack), m_log(std::move(log)) {}

void MissionSelectMode::launch(std::string_view /*onCancel*/, std::string_view /*onChoose*/) {}
void MissionSelectMode::enter() {}
ModeResult MissionSelectMode::update(GameModeStack& /*stack*/, const FrameTime& /*frame*/) { return ModeResult::Stay; }
void MissionSelectMode::render(const RenderTime& /*time*/) {}
void MissionSelectMode::exit() {}
void MissionSelectMode::resetPlayersForChoice() {}

} // namespace coney
