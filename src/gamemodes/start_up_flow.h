// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string_view>

#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/legal_screen_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "gamemodes/memory_card_mode.h"
#include "gamemodes/profile_manager_mode.h"
#include "graphics/render_device.h"
#include "gui/global_strings.h"

namespace coney {

/// The game's start-up path from the movies to the main menu, as `main` sets it up: the modes it pushes (8, 6, 5) and
/// the one the front end pushes (0x12), with the services they share. `coney --disc` and the disc test build it the
/// same way.
///
/// Research: docs/research/boot.md#main, docs/research/frontend.md#mode-flow
class StartUpFlow {
  public:
    /// The modes over `device`, sheets from `loadSheet` and `strings`, run on `stack`; `legal` picks the legal
    /// screen's picture and its `europe` flag also hides PM_Extras. Every argument must outlive the flow; `log` gets
    /// the modes' and the services' lines.
    StartUpFlow(graphics::RenderDevice& device, GameModeStack& stack, const ProfileManagerMode::SheetLoader& loadSheet,
                const gui::GlobalStrings& strings, LegalScreenSettings legal,
                const std::function<void(std::string_view)>& log);

    /// What `main` does from step 7 on: plays the start-up movies (skipped: FrontEndServices), pushes the level flow,
    /// asks for the memory-card boot check, pushes the memory-card mode, then the legal screen, which runs first.
    /// Coney leaves out the controller check and its error mode (the pads are always read).
    void start();

    /// The services the modes share.
    [[nodiscard]] FrontEndServices& services() { return m_services; }
    /// Mode 5.
    [[nodiscard]] LegalScreenMode& legal() { return m_legal; }
    /// Mode 6.
    [[nodiscard]] MemoryCardMode& memoryCard() { return m_memoryCard; }
    /// Mode 8.
    [[nodiscard]] LevelFlowMode& levelFlow() { return m_levelFlow; }
    /// Mode 0x12.
    [[nodiscard]] ProfileManagerMode& profileManager() { return m_profileManager; }

  private:
    GameModeStack& m_stack;
    FrontEndServices m_services;
    ProfileManagerMode m_profileManager;
    LevelFlowMode m_levelFlow;
    MemoryCardMode m_memoryCard;
    LegalScreenMode m_legal;
};

} // namespace coney
