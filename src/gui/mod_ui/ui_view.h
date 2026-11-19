// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "graphics/sprite_batch.h"
#include "gui/mod_ui/ui_model.h"
#include "gui/text_layout.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// Draws the UI model with the game's own widget classes: each shown screen's widgets in the order they were added,
/// the dialogs over them, and above everything the **frame label**, the project's title, in the trusted layer.
///
/// - **Which screens**: the top screen of each slot's flow (a covered screen is not drawn, as the screen flow draws
///   only its top) and every shown HUD screen.
/// - **Two layers**: the mods' widgets go into batches at the game's menu depths (sprites kSpriteDepth, text
///   kTextDepth); the frame labels into batches of their own at kLabelDepth, above every depth Coney's 2D pass uses,
///   so no widget of any project, nor any part of the game's own HUD, can be drawn over a label.
/// - **The frame label** (docs/guides/studio/ui.md#no-spoofing): the registered title of the project of every shown
///   mod screen, menu or HUD, each project once, small, right-aligned at the top right of the safe area, one line
///   under another in the order slot 1's top screen, slot 2's, then the HUD screens. Its text comes only from
///   UiModel::registerProject(); no widget call reaches it.
///
/// Coney's choices: the label's place, size and colour (below); a list's rows are texts at the grid's row pitch, the
/// selected one in kSelectedGrey; a dialog is laid out as the message box's choice dialog (MessageBox's kChoiceY and
/// kUsageY); a keyboard's cells at PM_Create's pitches.
///
/// Presentation: it reads the model and never changes it. list() runs in the host's update, as the game's GUI lists
/// its sprites in the step; the host's render draws the batches.
///
/// Research: docs/guides/studio/ui.md#widgets
/// @coney-only the UI widget model's renderer; the original has no mod screens
class UiView {
  public:
    /// The depth of the mods' sprites: the game's menu sprites (`menu_system`, 8,500).
    static constexpr float kSpriteDepth = 8500.0F;
    /// The depth of the mods' text: the game's menu text (9,000).
    static constexpr float kTextDepth = 9000.0F;
    /// The depth of the trusted layer: above every 2D depth Coney draws at (its highest, 20,001, is the HUD's lock-pick
    /// dial face, src/platform/hud_layer.cpp).
    static constexpr float kLabelDepth = 30000.0F;
    /// The frame label's place (Coney's choice): its right edge and first line, the gap to the next, its font scale.
    static constexpr float kLabelX = 1.0F;
    static constexpr float kLabelY = 0.02F;
    static constexpr float kLabelLineGap = 0.035F;
    static constexpr float kLabelScale = 0.8F;
    /// A sprite's height in overlay units per unit of its definition's size (Coney's choice: size 1 is a tenth).
    static constexpr float kSpriteHeightPerSize = 0.1F;
    /// Sprites a batch holds a frame.
    static constexpr std::size_t kTextCapacity = 4096;
    static constexpr std::size_t kSpriteCapacity = 512;

    /// Finds a sprite sheet by its `game:/` name; null when there is none.
    using SheetLookup = std::function<const graphics::SpriteSheet*(std::string_view name)>;

    /// A view drawing text with `fonts` and sprites from `sheets`.
    /// @coney-only the UI widget model's renderer
    UiView(FontLookup fonts, SheetLookup sheets);
    ~UiView() = default;
    // Not copied or moved: the canvases' batch callbacks hold `this`.
    UiView(const UiView&) = delete;
    UiView& operator=(const UiView&) = delete;
    UiView(UiView&&) = delete;
    UiView& operator=(UiView&&) = delete;

    /// Empties the batches and lists the sprites of `model`'s shown screens and their frame labels at `frame`.
    /// @coney-only the UI widget model's renderer
    void list(const UiModel& model, const GuiFrame& frame);
    /// Queues every batch with sprites on `pass`.
    /// @coney-only the UI widget model's renderer
    void queue(graphics::OverlayPass& pass);

    /// The mods' text batch of font slot `slot`, or null when nothing was drawn in it.
    [[nodiscard]] const graphics::SpriteBatch* textBatch(int slot) const;
    /// The trusted layer's text batch of font slot `slot`, or null.
    [[nodiscard]] const graphics::SpriteBatch* labelBatch(int slot) const;
    /// The mods' sprite batch of sheet `sheet`, or null.
    [[nodiscard]] const graphics::SpriteBatch* spriteBatch(std::string_view sheet) const;
    /// The labels listed by the last list(), top line first.
    [[nodiscard]] const std::vector<std::string>& labels() const { return m_labelTexts; }

  private:
    // The batch of font slot `slot` in `batches`, made at `depth` the first time.
    graphics::SpriteBatch* fontBatch(std::map<int, std::unique_ptr<graphics::SpriteBatch>>& batches, int slot,
                                     float depth);
    // The sprite batch of sheet `sheet`, made the first time; null without the sheet.
    graphics::SpriteBatch* sheetBatch(const std::string& sheet);
    // Lists one screen's widgets and dialog.
    void listScreen(const UiScreen& screen, const GuiFrame& frame);
    // Lists one widget.
    void listWidget(const UiScreen& screen, const UiWidget& w, const GuiFrame& frame);
    // Lists a text at a place and colour through the mods' canvas, kept by `key` so its time runs on.
    void listText(const std::string& key, const UiWidgetDef& def, std::string_view text, float x, float y,
                  graphics::Rgba colour, const GuiFrame& frame);
    // Lists `title` as the next frame label line, unless a line already shows it.
    void listLabel(std::string_view title, const GuiFrame& frame);

    FontLookup m_fonts;
    SheetLookup m_sheets;
    GuiCanvas m_canvas;      // the mods' layer
    GuiCanvas m_labelCanvas; // the trusted layer
    std::map<int, std::unique_ptr<graphics::SpriteBatch>> m_textBatches;
    std::map<int, std::unique_ptr<graphics::SpriteBatch>> m_labelBatches;
    std::map<std::string, std::unique_ptr<graphics::SpriteBatch>, std::less<>> m_spriteBatches;
    std::map<std::string, std::unique_ptr<Widget>, std::less<>>
        m_widgets; // the game's widgets, by a key per drawn part
    std::vector<std::string> m_labelTexts;
    std::set<std::string, std::less<>> m_drawn; // the keys of m_widgets drawn this frame; the rest go after it
};

} // namespace coney::gui
