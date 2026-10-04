// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "gui/text_widget.h"

namespace coney::gui {

/// The button legend line at the foot of a menu ("select", "ok", "back" with their button glyphs): a text widget the
/// menus fill with global string `0x1f`.
///
/// Coney's choice: a centred text widget at the GUI y its owner gives; the original's widget (`0x2d0` bytes) has a
/// layout not on the page.
///
/// Research: docs/research/gui.md#widgets, docs/research/frontend.md#input
/// @orig 0x001cea70 UsageInfo::UsageInfo (unknown)
class UsageInfo : public TextWidget {
  public:
    /// The global string of the menus' usage line.
    static constexpr std::uint32_t kMenuUsageString = 0x1f;

    /// Sets the legend's text and centres it on GUI x 0.5 at GUI y `y`.
    /// @orig 0x001cec28 UsageInfo_SetText (unknown)
    void setLegend(std::string_view text, float y);
};

} // namespace coney::gui
