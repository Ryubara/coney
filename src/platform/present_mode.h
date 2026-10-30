// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How the GPU renderer shows its frames (docs/engine/renderer.md#frame-rate): the present mode chosen from `--vsync`
// and `--fps-cap`. No SDL type appears here, so the choice can be tested; src/platform/gpu_presenter.cpp asks SDL which
// modes the window has and sets the chosen one.

#include <cstdint>
#include <string_view>

namespace coney::platform {

/// A present mode, as SDL3's GPU API offers them (SDL_GPUPresentMode).
enum class PresentMode : std::uint8_t {
    Vsync,     ///< Each frame waits for the vertical blank: no tearing, the display's rate at most. Always offered.
    Mailbox,   ///< The newest frame is shown at the next vertical blank, older ones dropped: uncapped, no tearing.
    Immediate, ///< Each frame is shown at once: may tear; what a variable-refresh display or a capped rate wants.
};

/// The modes a window offers beyond Vsync, which every window has.
struct PresentModes {
    bool mailbox = false;   ///< PresentMode::Mailbox is offered.
    bool immediate = false; ///< PresentMode::Immediate is offered.
};

/// The present mode for `vsync` (`--vsync`) and `fpsCap` (`--fps-cap`, 0 for none) on a window offering `offered`:
/// Vsync while vsync is on; with it off, Mailbox when nothing caps the rate (uncapped without tearing) and Immediate
/// under a cap (the frame pacer holds the rate and a variable-refresh display follows each frame, as OpenGL's swap
/// interval 0 does). A mode the window does not offer falls back to the other of those two, then to Vsync.
[[nodiscard]] PresentMode choosePresentMode(bool vsync, std::uint32_t fpsCap, PresentModes offered);

/// The mode's name for the run log: `vsync`, `mailbox` or `immediate`.
[[nodiscard]] std::string_view presentModeName(PresentMode mode);

} // namespace coney::platform
