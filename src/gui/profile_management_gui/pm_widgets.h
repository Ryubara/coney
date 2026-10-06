// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "gui/option_grid.h"
#include "gui/profile_management_gui/pm_shared.h"
#include "gui/text_widget.h"
#include "gui/usage_info.h"

namespace coney::gui {

/// The profile manager's common widgets as every PM screen's `Init` builds them (docs/research/frontend.md#pm-layout):
/// left-aligned at the layout's x, `big_font`, the front end's red.
///
/// Research: docs/research/frontend.md#pm-layout, docs/research/frontend.md#pm-screens
namespace pm {

/// A title's font scale.
inline constexpr float kTitleScale = 1.4F;
/// Every grid item's font scale (it is not a selection scale).
inline constexpr float kItemScale = 1.15F;
/// The font scale of prompts and texts the screens show at the items' size (PM_Greet's "press START").
inline constexpr float kPromptScale = 1.15F;
/// A grid move.
inline constexpr int kMoveCue = 5;
/// A move or entry refused.
inline constexpr int kRefusedCue = 0xe;
/// Accept.
inline constexpr int kAcceptCue = 9;
/// Back.
inline constexpr int kBackCue = 0xf;

/// Sets `title` up as a PM title showing `text`: at (layout x, `y`), scale 1.4, kMenuRed, `big_font`.
void setupTitle(TextWidget& title, const PmShared& shared, std::string_view text, float y);

/// Sets `grid` up as a PM grid: rows `rows` from GUI y `y`, packed left from the layout's x, cue kMoveCue for a move
/// (and kRefusedCue for a refused one) through the shared sound.
void setupGrid(OptionGrid& grid, PmShared& shared, float y, std::vector<std::size_t> rows);

/// A PM grid item: `text` returning `code`, followed by the red `" : "` when `separator`, scale 1.15, kMenuRed,
/// `big_font`.
[[nodiscard]] OptionGridItem item(std::string_view text, int code, bool separator = false);

/// Sets `usage` up as a PM usage line: left-aligned at (layout x, layout usageY), string 0x1f.
void setupUsage(UsageInfo& usage, const PmShared& shared);

} // namespace pm

} // namespace coney::gui
