// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/message_paging.h"

#include <algorithm>
#include <optional>

namespace coney::gui {

namespace {

// The break the word wrap inserts and the page clip splits at.
constexpr std::string_view kWrapBreak = "<CRM>";

} // namespace

WrappedMessage wrapMessage(std::string_view text, float wrapWidth, const LineMeasure& measure) { return {}; }

std::string clipToPage(std::string_view wrapped, const MessagePage& page) { return {}; }

bool pageUp(MessagePage& page) { return {}; }

bool pageDown(MessagePage& page, std::size_t breaks) { return {}; }

LineMeasure lineMeasure(const TextStyle& style, const FontLookup& fonts) { return {}; }

} // namespace coney::gui
