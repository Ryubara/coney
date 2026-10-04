// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/usage_info.h"

namespace coney::gui {

namespace {

// The legend's box: nearly the whole GUI width, so a long legend stays centred.
constexpr float kBoxWidth = 0.9F;

} // namespace

void UsageInfo::setLegend(std::string_view text, float y) {
    setText(text);
    centreOn(0.5F, y, kBoxWidth);
}

} // namespace coney::gui
