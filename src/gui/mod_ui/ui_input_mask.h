// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/pad.h"
#include "core/pads.h"
#include "gui/mod_ui/ui_model.h"

namespace coney::gui {

/// The input mask for mod screens over play: sits between the input source and the game, as the debug menu's input
/// gate does, and hides from gameplay the pad of each player whose mod screen has it (UiModel::takesPad()).
///
/// - **While a slot's menu screen is open** gameplay sees that port connected with nothing pressed and the sticks
///   centred; the other port passes through, so player 2 plays on while player 1 is in a menu.
/// - **After it closes**, the buttons still held stay hidden from gameplay until they are let go, so the press that
///   closed a screen never acts in play.
/// - The screens read the ports as the device gave them (rawPad()).
///
/// It reads and counts frames only, never a clock, so a scripted run drives the screens exactly as it drives the game.
///
/// Research: docs/guides/studio/ui.md#focus
/// @coney-only the UI widget model's input mask; the original's menus are modes on top, which no mod screen over live
/// play can be
class UiInputMask final : public InputSource {
  public:
    /// A mask over `inner` (null: no pads) for the screens of `model`; both must outlive the mask.
    /// @coney-only the UI widget model's input mask
    UiInputMask(InputSource* inner, const UiModel& model) : m_inner(inner), m_model(model) {}

    /// The sample of frame `frame` with the screens' ports taken out, as described above.
    /// @coney-only the UI widget model's input mask
    [[nodiscard]] PortSamples sample(std::uint64_t frame) override;

    /// Port `port` as the device gave it, for the screens.
    [[nodiscard]] const Pad& rawPad(std::size_t port) const { return m_raw.at(port); }
    /// The buttons of `port` hidden from gameplay this frame.
    [[nodiscard]] std::uint16_t hiddenButtons(std::size_t port) const { return m_hidden.at(port); }

  private:
    InputSource* m_inner;
    const UiModel& m_model;
    std::array<Pad, kPadPorts> m_raw{};
    std::array<std::uint16_t, kPadPorts> m_hidden{}; // held buttons hidden until released
};

} // namespace coney::gui
