// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <utility>

#include "gui/mod_ui/ui_model.h"
#include "gui/screen_flow_controller.h"
#include "gui/widget.h"

namespace coney::gui {

/// A mod screen hosted on a menu's screen flow (the profile manager's on the front end): a screen-flow state that runs
/// one player slot's mod screens of the UI model and gives the flow back when they are all closed, so a mod screen
/// opened from a menu returns to it with back, as the game's screens do.
///
/// - **Update**: one frame of the slot's input (UiModel::step()) with the frame's time and pad; kStay while the slot's
///   flow holds a screen, kBack (the flow pops this state) once none is left.
/// - The host's render draws UiView's batches; the state draws nothing itself.
///
/// Research: docs/guides/studio/ui.md#widgets
/// @coney-only the UI widget model's host on a menu's screen flow; the original's flows hold only its own screens
class UiFlowScreen final : public ScreenFlowState {
  public:
    /// The frame (time and pad) of the step, from the menu that hosts the flow.
    using FrameSource = std::function<GuiFrame()>;

    /// A state running `model`'s screens for `slot`, its frames from `frame`; `model` must outlive it.
    /// @coney-only the UI widget model's host on a menu's screen flow
    UiFlowScreen(UiModel& model, std::size_t slot, FrameSource frame)
        : m_model(model), m_slot(slot), m_frame(std::move(frame)) {}

    /// The state's name in logs and tests.
    [[nodiscard]] std::string_view name() const override { return "UI_ModScreen"; }
    /// One frame of the slot's mod screens; kBack once none is open.
    /// @coney-only the UI widget model's host on a menu's screen flow
    int update() override;

  private:
    UiModel& m_model;
    std::size_t m_slot;
    FrameSource m_frame;
};

} // namespace coney::gui
