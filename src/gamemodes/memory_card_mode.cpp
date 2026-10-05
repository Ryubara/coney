// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/memory_card_mode.h"

#include "gamemodes/game_mode_stack.h"

namespace coney {

ModeResult MemoryCardMode::update(GameModeStack& /*stack*/, const FrameTime& /*frame*/) { return ModeResult::Leave; }

void MemoryCardMode::render(const RenderTime& /*time*/) {
    m_device.beginFrame(graphics::kBlack);
    m_device.present();
}

void MemoryCardMode::exit() {
    m_bootCheck = BootCheck::Done;
    // The stack calls exit() after removing this mode, so its top is the mode that was below.
    if (m_stack.top() == &m_levelFlow) {
        m_levelFlow.cancelFrontEndLoad();
    }
}

} // namespace coney
