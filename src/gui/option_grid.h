// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "graphics/render_device.h"
#include "gui/menu_input.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// Where an OptionGrid puts its items and how it marks the selected one. Every value is Coney's: the page gives the
/// selected item's scale (1.15) and nothing else of the layout (docs/research/gui.md#open-questions).
struct OptionGridLayout {
    float centreX = 0.5F;                             ///< GUI x the items are centred on.
    float top = 0.5F;                                 ///< GUI y of the first item's centre line.
    float rowGap = 0.07F;                             ///< GUI distance between two items' centre lines.
    float boxWidth = 0.8F;                            ///< Width of each item's text box, GUI units.
    float scale = 1.0F;                               ///< Font scale of an item.
    float selectedScale = 1.15F;                      ///< The selected item's scale, from the page (PM_Mode's grid).
    graphics::Rgba colour{160, 160, 160, 255};        ///< An item that is not selected.
    graphics::Rgba selectedColour = graphics::kWhite; ///< The selected item.
};

/// A grid of selectable items, each a text with the code it returns when chosen: the main menu's widget. The up and
/// down commands move the selection, accept returns the selected item's code.
///
/// Coney's choices where the page is silent (docs/research/gui.md#open-questions, "OptionGrid"): one column; the
/// selection wraps from the last item to the first and back; left and right do nothing; the code reaches the screen
/// as handle()'s result (the original passes a pointer that receives it, inferred).
///
/// Research: docs/research/gui.md#widgets, docs/research/frontend.md#profile-manager
/// @orig 0x001d3ef0 OptionGrid::OptionGrid (unknown)
class OptionGrid : public Widget {
  public:
    /// The most items a grid holds.
    static constexpr std::size_t kMaxItems = 50;

    /// Removes every item and sets the layout; the selection goes back to the first item.
    /// @orig 0x001d4110 OptionGrid_Setup (unknown)
    void setup(const OptionGridLayout& layout);

    /// Appends an item showing `text` that returns `code` when chosen. Returns false and adds nothing when the grid
    /// already holds kMaxItems.
    /// @orig 0x001d4230 OptionGrid_AddItem (unknown)
    bool addItem(std::string_view text, int code);

    /// Selects item `index`, which must be below items() (CONEY_ASSERT).
    void select(std::size_t index);

    /// Takes the input focus at game time `nowMs`: `input` forgets its last command and ignores the d-pad briefly.
    /// @orig 0x001d4d28 OptionGrid_TakeFocus (unknown)
    void takeFocus(MenuInput& input, std::uint64_t nowMs);

    /// Acts on one menu command: up and down move the selection; accept returns the selected item's code. Returns
    /// nothing for every other command, and for an empty grid.
    [[nodiscard]] std::optional<int> handle(MenuCommand command);

    /// Advances the items' text time.
    void update(const GuiFrame& frame) override;
    /// Draws every item, the selected one at the selected scale and colour. Nothing while hidden.
    void render(const GuiCanvas& canvas) const override;

    /// How many items there are.
    [[nodiscard]] std::size_t items() const { return m_items.size(); }
    /// The selected item's index.
    [[nodiscard]] std::size_t selected() const { return m_selected; }
    /// The code of item `index` (CONEY_ASSERT that it exists).
    [[nodiscard]] int code(std::size_t index) const;
    /// The text widget of item `index` (CONEY_ASSERT that it exists), placed and styled as render() draws it.
    [[nodiscard]] const TextWidget& item(std::size_t index) const;

  private:
    // One item: its text widget and the code it returns.
    struct Item {
        TextWidget text;
        int code = 0;
    };
    // Places and styles every item for the current selection.
    void restyle();

    OptionGridLayout m_layout;
    std::vector<std::unique_ptr<Item>> m_items; // pointers: a TextWidget cannot move
    std::size_t m_selected = 0;
};

} // namespace coney::gui
