// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "graphics/render_device.h"
#include "gui/colour_table.h"
#include "gui/menu_input.h"
#include "gui/text_layout.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// How an OptionGrid is set up: the original's setup (`0x001d4110(y, grid, rows, a2, owner)`) and the fields its owners
/// set after it (`+0x8c` left x with `+0x88` left-packing, the move cue `+0x94`).
struct OptionGridSetup {
    float y = 0.5F;                  ///< GUI y the first row's items are centred on (`+0x28`).
    std::vector<std::size_t> rows{}; ///< Items per row, in order (at most 5); items past them get a row each.
    float centreX = 0.5F;            ///< Each row is centred on this GUI x (`+0x90`) unless leftX is set.
    std::optional<float> leftX{};    ///< Rows packed left to right from this GUI x (`+0x8c` with `+0x88` = 1).
    int moveCue = 4;                 ///< The front-end sound cue of a move (`+0x94`).
    bool stayInRow = false;          ///< Left and right stay in the row (`+0x9c` set) instead of walking all items.
    float rowGap = 0.0F;             ///< Added between rows (`+0xa0`).
    std::function<void(int cue)> playCue{}; ///< Plays a front-end sound cue; may be empty.
};

/// One item of an OptionGrid: the original's add item (`0x001d4230(fontScale, grid, text, hasSeparator, code, flags,
/// enabled, colour, fontSlot, separatorAlphaMode)`).
struct OptionGridItem {
    std::string text{};
    int code = 0;                     ///< What the owner reads when it is chosen (`OptionGridItem` `+0x54`).
    bool separator = false;           ///< Followed by `" : "` in part_page0 and the item's colour.
    bool enabled = true;              ///< Can be selected; moves skip a disabled item.
    graphics::Rgba colour = kMenuRed; ///< The colour when not selected.
    float scale = 1.15F;              ///< The font scale.
    int fontSlot = kBigFontSlot;      ///< The font.
};

/// A grid of selectable text items, each with a code: the menus' option widget.
///
/// - **Layout** (each frame): rows in order. With leftX the items of a row are packed left to right from it with no
///   gap, otherwise each row is centred on centreX. An item's box is its text's width (plus its separator's) by
///   h + lineGap = 6h / 7 of its font size; the row y moves by that height plus rowGap. Texts start at their x and are
///   centred on the row's y.
/// - **Colour**: the selected item, when enabled and the grid focused, is kSelectedGrey; every other item its own
///   colour; the alpha is kMenuGrey's. A separator keeps the item's colour. No highlight sprite and no size change:
///   selection is colour only.
/// - **Moves** (after the owner's handler, which sees every command first): up and down (with two rows or more) keep
///   the column, clamped to the row's length, wrap from the first row to the last and back, and skip items that cannot
///   be selected; left and right walk all items in order with wrap, or stay in the row with stayInRow. A grid of
///   fewer than 3 items does not wrap. A move plays moveCue; a move that cannot leave its item plays kRefusedCue and
///   switches the d-pad to its plain (not repeating) query until the next move.
///
/// Coney's choices: the row pitch adds kRowPitchExtra to the code's 6h / 7, so the rows land where they are measured
/// on screen (0.0505 apart at size 1.15, docs/research/frontend.md#pm-layout, an open question there); items past the
/// row counts get a row each.
///
/// Research: docs/research/gui.md#widget-classes, docs/research/frontend.md#pm-layout
/// @orig 0x001d3ef0 OptionGrid::OptionGrid (unknown)
class OptionGrid : public Widget {
  public:
    /// The most items a grid holds.
    static constexpr std::size_t kMaxItems = 50;
    /// The most row counts setup() keeps.
    static constexpr std::size_t kMaxRowCounts = 5;
    /// The cue of a move that cannot leave its item.
    static constexpr int kRefusedCue = 0xe;
    /// The separator after an item that has one (`0x00555f98`).
    static constexpr std::string_view kSeparator = " : ";
    /// Coney's addition to the row pitch: the measured 0.0505 at size 1.15 less the code's 6h / 7 (0.0469).
    static constexpr float kRowPitchExtra = 0.0036F;

    /// Removes every item and sets the grid up; the first item will be selected, without a sound.
    /// @orig 0x001d4110 OptionGrid_Setup (unknown)
    void setup(OptionGridSetup setup);

    /// Appends an item. Returns false and adds nothing when the grid already holds kMaxItems.
    /// @orig 0x001d4230 OptionGrid_AddItem (unknown)
    bool addItem(OptionGridItem item);

    /// Selects item `index`, which must be below items() (CONEY_ASSERT), without a sound (the original's
    /// `0x001d4b88(grid, i, 0)`).
    /// @orig 0x001d4b88 OptionGrid_Select (unknown)
    void select(std::size_t index);

    /// Takes the input focus at game time `nowMs`: `input` (which must outlive the grid's use of it) forgets its last
    /// command and blocks the d-pad briefly, and the selected item turns grey.
    /// @orig 0x001d4d28 OptionGrid_TakeFocus (unknown)
    void takeFocus(MenuInput& input, std::uint64_t nowMs);
    /// Gives the focus up: no item is drawn selected.
    /// @orig 0x001d4db8 OptionGrid_LoseFocus (unknown)
    void loseFocus() { m_focused = false; }
    /// Whether the grid has the focus.
    [[nodiscard]] bool focused() const { return m_focused; }

    /// Acts on one menu command the owner did not take: the moves above. Accept returns the selected item's code
    /// (the original's owners read it, `0x001d43f0(grid, 0x001d43e8(grid))`); nothing for every other command, and
    /// for an empty grid.
    /// @orig 0x001d4c40 OptionGrid_HandleCommand (unknown)
    [[nodiscard]] std::optional<int> handle(MenuCommand command);

    /// Advances the items' text time.
    void update(const GuiFrame& frame) override;
    /// Draws every item (and separator) where the layout puts it. Nothing while hidden.
    /// @orig 0x001d52c8 OptionGrid_Render (unknown)
    void render(const GuiCanvas& canvas) const override;

    /// How many items there are.
    [[nodiscard]] std::size_t items() const { return m_items.size(); }
    /// The selected item's index.
    [[nodiscard]] std::size_t selected() const { return m_selected; }
    /// The code of item `index` (CONEY_ASSERT that it exists).
    /// @orig 0x001d43f0 OptionGrid_Code (unknown)
    [[nodiscard]] int code(std::size_t index) const;
    /// Item `index` as added (CONEY_ASSERT that it exists).
    [[nodiscard]] const OptionGridItem& item(std::size_t index) const;
    /// The row of item `index` and its column in that row.
    [[nodiscard]] std::pair<std::size_t, std::size_t> cell(std::size_t index) const;
    /// How many rows the items fill.
    [[nodiscard]] std::size_t rows() const { return m_rowStarts.size(); }

    /// Where item `index` is drawn with the canvas's fonts: the GUI x its text starts at and the y it is centred on.
    [[nodiscard]] std::pair<float, float> itemPosition(std::size_t index, const GuiCanvas& canvas) const;
    /// The colour item `index` is drawn in now.
    [[nodiscard]] graphics::Rgba itemColour(std::size_t index) const;
    /// The GUI distance between two rows of items at font scale `scale`: 6h / 7 + kRowPitchExtra + rowGap.
    [[nodiscard]] float rowPitch(float scale) const;

  private:
    // Splits the items into rows by the setup's counts.
    void buildRows();
    // The width of item `index` (its text and separator) with the canvas's fonts.
    [[nodiscard]] float itemWidth(std::size_t index, const GuiCanvas& canvas) const;
    // Moves to item `index` with a cue, or plays the refused cue when it is the selected one.
    void moveTo(std::size_t index);
    // The item up or down (`step` -1 or 1) from the selection, keeping the column; the selection when none.
    [[nodiscard]] std::size_t verticalTarget(int step) const;
    // The item left or right (`step` -1 or 1) from the selection; the selection when none.
    [[nodiscard]] std::size_t horizontalTarget(int step) const;
    // Plays `cue`, if the grid has a way to.
    void playCue(int cue) const;

    OptionGridSetup m_setup;
    std::vector<OptionGridItem> m_items;
    std::vector<std::unique_ptr<TextWidget>> m_texts; // each item's text and its time; pointers, a widget cannot move
    std::vector<std::size_t> m_rowStarts;             // the first item of each row
    std::size_t m_selected = 0;
    bool m_focused = false;
    MenuInput* m_input = nullptr; // the focus's input, whose d-pad repeat a refused move turns off
};

} // namespace coney::gui
