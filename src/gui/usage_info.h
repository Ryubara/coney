// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "gui/text_widget.h"

namespace coney::gui {

/// The button legend at the foot of a menu ("select", "ok", "back" with their button glyphs): a markup text widget at
/// size 1.0 in kMenuGrey, font part_page0, left-aligned at its position (the profile manager's screens) or centred on
/// it (the Rumble screens, the message box).
///
/// Research: docs/research/gui.md#widget-classes, docs/research/frontend.md#pm-layout
/// @orig 0x001cea70 UsageInfo::UsageInfo (unknown)
class UsageInfo : public TextWidget {
  public:
    /// The global string of the menus' usage line (select, ok, back).
    static constexpr std::uint32_t kMenuUsageString = 0x1f;
    /// The global string a usage line shows before its owner sets one.
    static constexpr std::uint32_t kInitialString = 0x1a;
    /// The most bytes of a legend the widget keeps.
    static constexpr std::size_t kMaxLength = 0x95;
    /// The font scale of a usage line.
    static constexpr float kScale = 1.0F;

    /// Places the line: its first line centred on GUI y `y`, starting at GUI x `x` when `leftAlign`, else centred on
    /// it.
    /// @orig 0x001ceb40 UsageInfo_Setup (unknown)
    void place(float x, float y, bool leftAlign);

    /// Sets the legend's text (at most kMaxLength bytes).
    /// @orig 0x001cec28 UsageInfo_SetText (unknown)
    void setLegend(std::string_view text);
};

} // namespace coney::gui
