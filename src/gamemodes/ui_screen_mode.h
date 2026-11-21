// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "core/pad.h"
#include "gamemodes/game_mode.h"
#include "graphics/overlay_camera.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/mod_ui/ui_model.h"
#include "gui/mod_ui/ui_view.h"

namespace coney {

class OverlaidLevel;

/// A mod screen over play as a game mode of Coney's: pushed over gameplay while a screen that pauses play offline is on
/// a slot's flow (UiModel::pausesPlay(), from UiScreenDef::pause), it runs that slot's mod screens and leaves once none
/// pauses play, so gameplay is suspended under it as under the pause menu (docs/guides/studio/ui.md#focus).
///
/// - **Update**: the slot's input (UiModel::step()) at the step's game time with the slot's pad, then the view's
///   sprites for the step; Leave once no screen on the slot's flow pauses play.
/// - **Render**: the level as it was when the screen opened (through setWorld(), else black), with the screens'
///   batches over it.
///
/// A screen that does not pause play is not a mode: play goes on and only its player's pad is taken (UiInputMask).
///
/// Research: docs/guides/studio/ui.md#widgets
/// @coney-only the UI widget model's host over play; the original has no mod screens
class UiScreenMode final : public GameMode {
  public:
    /// The mode's id, among Coney's own.
    static constexpr std::uint32_t kId = 0x109;

    /// The pad of a port, as the step's input left it; null for none.
    using PadSource = std::function<const Pad*(std::size_t port)>;

    /// Runs `model`'s screens for `slot` with pads from `pads`, drawn by `view` through `device`; every reference must
    /// outlive the mode.
    /// @coney-only the UI widget model's host over play
    UiScreenMode(graphics::RenderDevice& device, gui::UiModel& model, gui::UiView& view, PadSource pads,
                 std::size_t slot);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Draws `world` (not owned; null: black) under the screens, frozen at game time `gameTicks`.
    void setWorld(OverlaidLevel* world, std::uint64_t gameTicks) {
        m_world = world;
        m_frozen = RenderTime{.alpha = 1.0F, .gameTicks = gameTicks, .index = 0};
    }

    /// One step of the slot's screens; leaves when none pauses play.
    /// @coney-only the UI widget model's host over play
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// The frozen level and the screens over it.
    /// @coney-only the UI widget model's host over play
    void render(const RenderTime& time) override;

  private:
    graphics::RenderDevice& m_device;
    gui::UiModel& m_model;
    gui::UiView& m_view;
    PadSource m_pads;
    std::size_t m_slot;
    graphics::OverlayCamera m_camera;
    graphics::OverlayPass m_pass;
    OverlaidLevel* m_world = nullptr;
    RenderTime m_frozen;
};

} // namespace coney
