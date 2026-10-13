// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace coney::debug {

/// The display height, in the developer overlay's units, at which its widgets have their designed size (scale 1): a
/// 1080p screen. A taller window scales them up in proportion.
inline constexpr float kUiScaleReferenceHeight = 1080.0F;
/// The smallest and the largest scale the overlay uses. The smallest is 1: a small window keeps the designed size
/// (text any smaller is hard to read) and scrolls instead.
inline constexpr float kUiScaleMin = 1.0F;
inline constexpr float kUiScaleMax = 4.0F;
/// The step the automatic scale is rounded to, so dragging a window edge changes the scale in a few steps and not on
/// every pixel (each step makes the font's glyphs again at the new size).
inline constexpr float kUiScaleStep = 0.125F;

/// The automatic scale of the developer overlay for a display `displayHeight` units tall (ImGui's display size: the
/// window's pixel height on Windows, its height in points on macOS): the height over 1080, rounded to a step of
/// kUiScaleStep and kept within kUiScaleMin and kUiScaleMax, so it is the same physical size on a 1080p, a 1440p and a
/// 4K display. A height of 0 or less (nothing measured yet) gives kUiScaleMin. Pure: no window or clock, so tests and
/// headless runs see the same.
[[nodiscard]] float autoUiScale(float displayHeight);

/// The labels of the Display page's scale choice: `Auto`, then each fixed scale (`1x`, `1.5x`, `2x`...). Index 0 is
/// always Auto.
[[nodiscard]] const std::vector<std::string>& uiScaleLabels();

/// The scale the choice `choice` (an index into uiScaleLabels()) gives on a display `displayHeight` units tall: Auto
/// gives autoUiScale(), a fixed choice its own scale whatever the height. An index past the end counts as Auto.
[[nodiscard]] float uiScaleFor(std::size_t choice, float displayHeight);

/// `scale` as the menus show it: `1x`, `1.5x`, `1.375x` (no trailing zeros).
[[nodiscard]] std::string uiScaleText(float scale);

} // namespace coney::debug
