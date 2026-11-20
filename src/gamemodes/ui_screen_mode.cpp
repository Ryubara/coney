// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#include "gamemodes/ui_screen_mode.h"

#include <utility>

#include "core/game_timer.h"
#include "gamemodes/gameplay_mode.h"

namespace coney {

UiScreenMode::UiScreenMode(graphics::RenderDevice& device, gui::UiModel& model, gui::UiView& view, PadSource pads,
                           std::size_t slot)
    : m_device(device), m_model(model), m_view(view), m_pads(std::move(pads)), m_slot(slot) {}

ModeResult UiScreenMode::update(GameModeStack& /*stack*/, const FrameTime& frame) {
    // The slot's input at the step's game time, then the step's sprites.
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    const Pad* pad = m_pads ? m_pads(m_slot) : nullptr;
    if (pad != nullptr) {
        m_model.step(m_slot, *pad, nowMs);
    }
    m_pass.empty();
    m_view.list(m_model, gui::GuiFrame{.timeMs = nowMs, .pad = pad});
    m_view.queue(m_pass);
    // Play resumes once no screen on the slot's flow pauses it; the screens left go on over live play.
    return m_model.pausesPlay(m_slot) ? ModeResult::Stay : ModeResult::Leave;
}

void UiScreenMode::render(const RenderTime& /*time*/) {
    const auto overlay = [this](graphics::RenderDevice& device) { m_pass.draw(device, m_camera); };
    if (m_world != nullptr) {
        m_world->renderWithOverlay(m_frozen, overlay);
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    overlay(m_device);
    m_device.present();
}

} // namespace coney
