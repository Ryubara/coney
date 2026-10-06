// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/pads.h"
#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"

namespace coney {

/// A 2D screen shown over a level while it plays, which play steps and draws but which is not a game mode of its own:
/// the original's HUD screens that run in mode 1 (the Rumble intro, `RM_Intro`). GameplayMode steps each one after the
/// scripts' frame and draws it over the level's frame while it is showing.
class PlayOverlay {
  public:
    virtual ~PlayOverlay() = default;
    PlayOverlay() = default;
    PlayOverlay(const PlayOverlay&) = delete;
    PlayOverlay& operator=(const PlayOverlay&) = delete;
    PlayOverlay(PlayOverlay&&) = delete;
    PlayOverlay& operator=(PlayOverlay&&) = delete;

    /// One frame of play at `frame`, with the pads of the step.
    virtual void playFrame(const FrameTime& frame, const Pads& pads) = 0;
    /// Whether it draws anything now.
    [[nodiscard]] virtual bool showing() const = 0;
    /// Draws the newest frame's sprites through `device` (over the level, before the present).
    virtual void draw(graphics::RenderDevice& device) = 0;
    /// The level ended (gameplay left): the screen closes without acting.
    virtual void levelEnded() {}
};

} // namespace coney
