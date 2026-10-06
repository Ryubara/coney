// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/usage_info.h"

#include "gui/colour_table.h"

namespace coney::gui {

void UsageInfo::place(float x, float y, bool leftAlign) {
    setup(TextWidgetSetup{
        .x = x,
        .y = y,
        .scale = kScale,
        .colour = kMenuGrey,
        .alignment = leftAlign ? TextAlignment::Left : TextAlignment::Centre,
        .fontSlot = kTextFontSlot,
    });
}

void UsageInfo::setLegend(std::string_view text) { setText(text.substr(0, kMaxLength)); }

} // namespace coney::gui
