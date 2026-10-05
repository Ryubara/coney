// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "gamemodes/game_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "graphics/render_device.h"

namespace coney {

class GameModeStack;

/// Game mode 6, the memory-card check, on its boot path. The original scans the memory cards, shows the "checking"
/// message and any dialog a missing, unformatted or full card calls for, and pops when the save system and its message
/// box are done; its exit marks the boot check done and, when the level flow is the mode below, tells it not to load
/// the front end on its next resume.
///
/// **Coney's choice:** Coney has no memory card (its saves will be files, and none exist yet), so the check is a
/// pass-through on the "no saved profile" path: one black frame, then it leaves. The message and dialogs it would show
/// with no card are an open question of the research (docs/research/frontend.md#open-questions); the exit is the
/// original's.
///
/// Research: docs/research/frontend.md#mode-flow
class MemoryCardMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 6;

    /// The boot-check flag: 0 none, 1 the boot check is to run, 2 it ran.
    enum class BootCheck : std::uint8_t { None = 0, Pending = 1, Done = 2 };

    /// Draws through `device`; `stack` is the stack this mode runs on and `levelFlow` the mode 8 its exit may find
    /// below it. Each must outlive the mode.
    MemoryCardMode(graphics::RenderDevice& device, const GameModeStack& stack, LevelFlowMode& levelFlow)
        : m_device(device), m_stack(stack), m_levelFlow(levelFlow) {}

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Asks for the boot check: what `main` does before pushing the mode.
    /// @orig 0x0015a270 MemoryCard_SetBootCheck (Gm_MemoryCard.cpp)
    void setBootCheck() { m_bootCheck = BootCheck::Pending; }
    /// The boot-check flag.
    [[nodiscard]] BootCheck bootCheck() const { return m_bootCheck; }

    /// Starts the check: nothing to scan (no memory card). A stand-in for the original's `Enter` (`0x0015baa0`), so it
    /// carries no original-function tag.
    void enter() override {}

    /// Leaves: the check is done. A stand-in for the original's `Update` (`0x0015be00`), so it carries no
    /// original-function tag.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// Clears the screen to black and presents it: the one frame the check shows.
    void render(const RenderTime& time) override;

    /// Marks the boot check done and, when the level flow is now on top (the mode below), cancels its front-end load
    /// on resume.
    /// @orig 0x0015c2c0 Mode6::Exit (Gm_MemoryCard.cpp)
    void exit() override;

  private:
    graphics::RenderDevice& m_device;
    const GameModeStack& m_stack;
    LevelFlowMode& m_levelFlow;
    BootCheck m_bootCheck = BootCheck::None;
};

} // namespace coney
