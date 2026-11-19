// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/ui_screen_mode.h"

#include <utility>

namespace coney {

UiScreenMode::UiScreenMode(graphics::RenderDevice& device, gui::UiModel& model, gui::UiView& view, PadSource pads,
                           std::size_t slot)
    : m_device(device), m_model(model), m_view(view), m_pads(std::move(pads)), m_slot(slot) {}

ModeResult UiScreenMode::update(GameModeStack&, const FrameTime&) { return ModeResult::Stay; }

void UiScreenMode::render(const RenderTime&) {}

} // namespace coney
