// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "graphics/render_device.h"

namespace coney::graphics {

/// A summary of a captured frame, for checks that must not keep the image itself.
struct FrameStats {
    std::uint64_t pixels = 0;        ///< Pixels in the frame.
    std::uint64_t notBackground = 0; ///< Pixels whose colour differs from the background (alpha ignored).
    std::uint64_t hash = 0;          ///< FNV-1a (64-bit) of the RGBA bytes, rows top first.
};

/// Summarises a frame of `rgba` pixels (4 bytes each, rows top first) against the colour it was cleared to.
/// `rgba.size()` must be a multiple of 4 (checked by CONEY_ASSERT).
[[nodiscard]] FrameStats summarizeFrame(std::span<const std::uint8_t> rgba, Rgba background);

} // namespace coney::graphics
