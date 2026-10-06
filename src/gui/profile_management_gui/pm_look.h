// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "graphics/render_device.h"
#include "gui/profile_management_gui/pm_screen.h"
#include "gui/profile_management_gui/pm_shared.h"
#include "gui/text_widget.h"

namespace coney::gui {

/// The look the story screens share, from docs/research/frontend.md#pm-layout (the default, interlaced 4:3 column):
/// where titles, grids and usage lines go, and their sizes and colours. Positions are GUI points, x the left edge.
namespace pm_look {
inline constexpr float kX = 0.0F;             ///< `0x0050f5c4`: every widget's left edge.
inline constexpr float kUsageY = 0.87F;       ///< `0x0050f5e8`: the usage line.
inline constexpr float kRowPitch = 0.0505F;   ///< Grid rows, as measured (the code's `6h / 7` gives 0.0469).
inline constexpr float kTitleScale = 1.4F;    ///< Titles.
inline constexpr float kItemScale = 1.15F;    ///< Grid items (every item; not a selection scale).
inline constexpr float kUsageScale = 1.0F;    ///< The usage line.
inline constexpr float kMessageScale = 0.85F; ///< Error and question texts.
inline constexpr float kNameScale = 2.0F;     ///< A profile name shown large (PM_Create, PM_Continue, PM_Delete).
/// `0x005fd328`: titles, unselected items, separators.
inline constexpr graphics::Rgba kRed{170, 43, 43, 255};
/// `0x005fd318` / `0x005fd310`: the selected item, the usage line, names.
inline constexpr graphics::Rgba kGrey{178, 178, 178, 255};
/// The text between two items of a row.
inline constexpr std::string_view kSeparator = " : ";

/// The grid's y for a grid of `rows` rows (`0x0050f5c8`-`0x0050f5d4`: 1 to 4 rows).
[[nodiscard]] float gridY(std::size_t rows);
/// The title's y over a grid of `rows` rows (`0x0050f5d8`-`0x0050f5e4`).
[[nodiscard]] float titleY(std::size_t rows);
} // namespace pm_look

/// Places `text` in `widget`: left edge at GUI x `x`, centre line at `y`, at `scale` in `colour`, font slot `slot`.
void placePmText(TextWidget& widget, std::string_view text, float x, float y, float scale, graphics::Rgba colour,
                 int fontSlot);

/// A PM screen's grid as drawn: each item a text in `big_font` at 1.15, red, the selected one grey, items packed left
/// to right in their rows with a red `" : "` after an item that has another after it. Coney's stand-in until the shared
/// `OptionGrid` with rows exists (owned by the GUI widgets' track).
class PmGridView {
  public:
    /// Rebuilds the texts for `choices` with the top row's centre line at GUI y `top`.
    void build(const PmChoices& choices, float top);
    /// Restyles for the current selection and draws on `canvas`.
    void render(const PmChoices& choices, const GuiCanvas& canvas);
    /// Releases the texts.
    void clear();
    /// The text of item `index`, for tests; CONEY_ASSERT that it exists.
    [[nodiscard]] const TextWidget& item(std::size_t index) const;

  private:
    std::vector<std::unique_ptr<TextWidget>> m_items;      // pointers: a TextWidget cannot move
    std::vector<std::unique_ptr<TextWidget>> m_separators; // one after each item followed by another in its row
    float m_top = 0.0F;
    bool m_placed = false; // the x positions need the fonts, so they are set on the first render
};

/// The usage line of every PM screen: global string `0x1f`, left-aligned at (x, 0.87), size 1.0, grey, `part_page0`.
void placePmUsage(TextWidget& widget, const PmShared& shared);

} // namespace coney::gui
