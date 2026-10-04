// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "gamemodes/game_mode.h"

namespace coney {

/// The bottom of the stack while Coney has no game to run: it stays forever, so the main loop runs until the window
/// is closed or the frame limit is reached. It stands where the original's mode 8 will (docs/research/boot.md#main).
class IdleMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range of ids.
    static constexpr std::uint32_t kId = 0x101;

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    ModeResult update(GameModeStack& /*stack*/, const FrameTime& /*frame*/) override { return ModeResult::Stay; }
};

} // namespace coney
