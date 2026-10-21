// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

#include "core/language.h"
#include "graphics/render_device.h"
#include "gui/base_widget.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/scrolling_menu.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// The Controls screen's strings (docs/research/pause.md#fn-controlmenuhud): the two entries, which are also their
/// pages' titles, the controller picture's labels (seven drawn ending at their place, seven starting there), and the
/// tutorial tips (title `0x145` + i, text `0x15c` + i).
namespace control_strings {
inline constexpr std::uint32_t kController = 0x130;
inline constexpr std::uint32_t kTutorial = 0x131;
inline constexpr std::uint32_t kLeftColumn = 0x132;  ///< `0x132`-`0x138`, left of the picture, right-aligned (5).
inline constexpr std::uint32_t kRightColumn = 0x139; ///< `0x139`-`0x13f`, right of the picture, left-aligned (4).
inline constexpr std::uint32_t kTipTitles = 0x145;
inline constexpr std::uint32_t kTipTexts = 0x15c;
} // namespace control_strings

/// How many labels each side of the controller picture has, and how many tutorial tips there are.
inline constexpr std::size_t kControllerLabels = 7;
inline constexpr std::size_t kTutorialTips = 23;

/// The Controls screen's places for one video mode (docs/research/pause.md#controls-layout): `ControllerPage_Init`
/// and `TutorialPage_Init` pick them by the device flags and the language (`W_GameState + 0x120`).
struct ControlLayout {
    /// Where the right-aligned labels (`0x132`-`0x138`) end and the left-aligned ones (`0x139`-`0x13f`) start.
    float leftColumnX = 0.235F;
    float rightColumnX = 0.765F;
    /// Label 1's x (`0x0063ed60`), which the language moves.
    float labelOneX = 0.235F;
    /// The rows both columns share (the table `0x0050ef20`), wider after rows 1 and 5 for two-line labels.
    std::array<float, 7> rows{0.355F, 0.395F, 0.48F, 0.522F, 0.565F, 0.607F, 0.68F};
    /// The pad picture's centre y and size.
    float pictureY = 0.58F;
    float pictureWidth = 0.51F;
    float pictureHeight = 0.45F;
    /// The tips shown at once (`0x0050ef00`), the list's clip height (`0x0050ef0c`) and the items' spacing
    /// (`0x0050eeec`).
    std::size_t visibleTips = 8;
    float tipClipHeight = 0.5F;
    float tipSpacing = 0.01F;

    /// The layout for `language` in the default video mode (device flag `0x01`), in 16:9 (`widescreen`, flag `0x04`)
    /// and with flag `0x02` (`flag02`). Flag `0x08` is not modelled (Coney has no such device).
    /// @orig 0x001e4130 ControllerPage_Init (ControlMenuHUD.cpp)
    /// @orig 0x001e3828 TutorialPage_Init (ControlMenuHUD.cpp)
    [[nodiscard]] static ControlLayout forMode(Language language, bool widescreen, bool flag02);
};

/// Which page of the Controls screen is open.
enum class ControlPage : std::uint8_t {
    None,       ///< The two entries.
    Controller, ///< The pad picture with each button's action.
    Tutorial,   ///< The list of tips.
};

/// The pause menu's Controls screen (`ControlMenuHUD`, a `GameMenu`): two entries, **Controller** and **Tutorial**,
/// each opening its page over the screen.
///
/// - **Entries** (`ControlMenuHUD_Open`): Controller at (0.26, 0.29), Tutorial 0.06 below, size 1.3; the selected one
///   in `0x005fd310`, the other in `0x005fd320`. Up and down move (cue 4, `0xe` at an end), left and right refuse
///   (`0xe`), accept opens the page (cue 8 for Controller), back plays `0xf` and leaves the screen.
/// - **A page** takes every command; back (cue `0xf`) closes it and the entries show again.
/// - **A page's title** at (0.26, 0.29), size 1.3, `0x005fd310`, font slot 4 (`ControlPage_Init`).
/// - **Controller** (`ControllerPage`): the title `0x130`, the pad picture (sheet-table record `0x18f` rectangle 0,
///   centred at (0.5, 0.58), 0.51 × 0.45, depth 11,000) and its 14 labels (size 1.0, `0x005fd310`, font slot 3) at
///   the layout's rows, seven ending at x 0.235 and seven starting at x 0.765; a two-line label grows downward into
///   the gap the rows leave under it. Directions refuse (`0xe`).
/// - **Tutorial** (`TutorialPage`): the title `0x131` and a ScrollingMenu of the 23 tips at (0.41, 0.35), 8 visible,
///   clipped to 0.5 high, wrapping at 0.53, 0.01 apart; the focused tip red (134, 26, 26), the others `0x005fd320`;
///   550 ms after a tip is focused its text unfolds 0.01 under it (cue `0xd` as it starts, a quarter of its height a
///   frame) and the tips below move down.
///
/// Coney's choices where the page is silent: the list's window jumps to its new place (ScrollingMenu), and the text
/// is drawn once it has fully unfolded (the tips below make room for it as it unfolds).
///
/// Research: docs/research/pause.md#fn-controlmenuhud
/// @orig 0x001e4908 ControlMenuHUD_Construct (ControlMenuHUD.cpp)
class ControlMenu {
  public:
    /// The controller picture's sheet-table record (sprite word `0x18f0000`) and its depth.
    static constexpr std::uint32_t kPictureRecord = 0x18f;
    static constexpr float kPictureDepth = 11000.0F;
    /// The entries' place and step, and their size.
    static constexpr float kEntryX = 0.26F;
    static constexpr float kEntryY = 0.29F;
    static constexpr float kEntryStep = 0.06F;
    static constexpr float kEntrySize = 1.3F;
    /// The pages' title place (`0x0050eee4`, `0x0050eee8`) and font slot.
    static constexpr float kTitleX = 0.26F;
    static constexpr float kTitleY = 0.29F;
    static constexpr int kTitleFontSlot = 4;
    /// The labels' size and font slot.
    static constexpr float kLabelSize = 1.0F;
    static constexpr int kLabelFontSlot = 3;
    /// The tips' place (`0x0050eef0`, `0x0050eef4`), wrap width (`0x0050eef8`), size and font slot. The place is the
    /// centre of each 0.3 wide `ScrollingTextWidget` (`ScrollingTextWidget_Update` `0x001e32a0` draws its text at
    /// x - width / 2), so the text starts at kTipsX - kTipWidth / 2 = 0.26, the title's x (confirmed at runtime: the
    /// original's first tip starts where the "Tutorial" title does).
    static constexpr float kTipsX = 0.41F;
    static constexpr float kTipWidth = 0.3F;
    static constexpr float kTipsY = 0.35F;
    static constexpr float kTipWrap = 0.53F;
    static constexpr float kTipSize = 1.0F;
    static constexpr int kTipFontSlot = 3;
    /// How long a tip is focused before its text unfolds (the item's `+0x6c`), the gap above the text
    /// (`0x0050eefc`) and how much of it unfolds a frame (`+0x70`).
    static constexpr std::uint64_t kDescriptionDelayMs = 550;
    static constexpr float kDescriptionGap = 0.01F;
    static constexpr float kRevealStep = 0.25F;
    /// The focused tip's colour.
    static constexpr graphics::Rgba kTipFocus{134, 26, 26, 255};

    ControlMenu();

    /// Opens the screen on its entries, Controller selected, no page open: the entries' texts and places, both pages
    /// set up (`ControllerPage_Init`, `TutorialPage_Init`) with `layout`'s places. `strings` and `input` must outlive
    /// the menu's use; `playCue` plays a front-end cue and may be empty.
    /// @orig 0x001e4a30 ControlMenuHUD_Open (ControlMenuHUD.cpp)
    /// @orig 0x001e4130 ControllerPage_Init (ControlMenuHUD.cpp)
    /// @orig 0x001e3828 TutorialPage_Init (ControlMenuHUD.cpp)
    /// @orig 0x001e3520 ControlPage_Init (ControlMenuHUD.cpp)
    void open(const GlobalStrings* strings, MenuInput& input, std::function<void(int cue)> playCue, std::uint64_t nowMs,
              const ControlLayout& layout = {});

    /// Acts on one menu command at `nowMs`. Returns true when back leaves the screen (from the entries).
    /// @orig 0x001e4f18 ControlMenuHUD_OnCommand (ControlMenuHUD.cpp)
    [[nodiscard]] bool handle(MenuCommand command, std::uint64_t nowMs);

    /// The tips' focus time and text reveal.
    /// @orig 0x001e5130 ControlMenuHUD_Update (ControlMenuHUD.cpp)
    void update(const GuiFrame& frame);
    /// Adds the entries, or the open page.
    /// @orig 0x001e5300 ControlMenuHUD_Render (ControlMenuHUD.cpp)
    /// @orig 0x001e4820 ControllerPage_Render (ControlMenuHUD.cpp)
    /// @orig 0x001e3f38 TutorialPage_Render (ControlMenuHUD.cpp)
    void render(const GuiCanvas& canvas) const;
    /// The menu's alpha, 0 to 1, on every text and the picture.
    /// @orig 0x001e52f8 ControlMenuHUD_SetAlpha (ControlMenuHUD.cpp)
    void setFade(float fade);
    /// Draws the controller picture into `batch` (the sheet of kPictureRecord at kPictureDepth); null draws none.
    void setPictureBatch(graphics::SpriteBatch* batch) { m_picture.setBatch(batch); }

    /// The selected entry (0 Controller, 1 Tutorial).
    [[nodiscard]] std::size_t selected() const { return m_selected; }
    /// The open page.
    [[nodiscard]] ControlPage page() const { return m_page; }
    /// The tutorial's list.
    [[nodiscard]] const ScrollingMenu& tips() const { return m_tips; }
    /// Whether the focused tip's text has started to unfold (550 ms after the focus).
    [[nodiscard]] bool descriptionShown() const { return m_descriptionShown; }
    /// How far the focused tip's text has unfolded, 0 to 1; it is drawn at 1.
    [[nodiscard]] float descriptionReveal() const { return m_reveal; }
    /// The layout the screen was opened with.
    [[nodiscard]] const ControlLayout& layout() const { return m_layout; }
    /// The texts shown now, in drawing order (for tests and the log): the entries, or the open page's title, labels
    /// or the tips of the list's window and the shown text (before render() clips the window to kTipClipHeight).
    [[nodiscard]] std::vector<std::string> lines() const;
    /// Entry `index` (0 Controller, 1 Tutorial).
    [[nodiscard]] const TextWidget& entry(std::size_t index) const { return m_entries.at(index); }
    /// The picture widget.
    [[nodiscard]] const BaseWidget& picture() const { return m_picture; }
    /// The open page's title.
    [[nodiscard]] const TextWidget& title() const { return m_title; }
    /// Controller label `index`: 0-6 the column left of the picture (`0x132`-`0x138`), 7-13 the one right of it.
    [[nodiscard]] const TextWidget& label(std::size_t index) const { return m_labels.at(index); }
    /// Tip `index`'s title widget (placed by the last render()).
    [[nodiscard]] const TextWidget& tip(std::size_t index) const { return m_tipTitles.at(index); }
    /// The focused tip's text widget (placed by the last render()).
    [[nodiscard]] const TextWidget& description() const { return m_description; }

  private:
    // String `id`, or empty without strings.
    [[nodiscard]] std::string_view string(std::uint32_t id) const;
    // Plays `cue`, if there is a sink.
    void playCue(int cue) const;
    // The entries' colours by the selection.
    void colourEntries();
    // One command to the controller page; true when back closes it.
    // @orig 0x001e4618 ControllerPage_OnCommand (ControlMenuHUD.cpp)
    bool controllerCommand(MenuCommand command);
    // One command to the tutorial page; true when back closes it.
    // @orig 0x001e3e60 TutorialPage_OnCommand (ControlMenuHUD.cpp)
    bool tutorialCommand(MenuCommand command, std::uint64_t nowMs);
    // Recolours the tips by the focus and stamps the focus time.
    void focusTip(std::uint64_t nowMs);

    const GlobalStrings* m_strings = nullptr;
    std::function<void(int cue)> m_playCue;
    std::array<TextWidget, 2> m_entries;
    TextWidget m_title; // the open page's title (ControlPage `+0x60`)
    BaseWidget m_picture{nullptr, 0};
    std::array<TextWidget, 2 * kControllerLabels> m_labels;
    ScrollingMenu m_tips;
    // The tips' and their texts' widgets; placed in render(), which stacks them under the focused tip's text.
    mutable std::array<TextWidget, kTutorialTips> m_tipTitles;
    mutable TextWidget m_description;
    std::size_t m_selected = 0;
    ControlPage m_page = ControlPage::None;
    ControlLayout m_layout;
    std::uint64_t m_focusMs = 0;
    std::uint64_t m_nowMs = 0;
    bool m_descriptionShown = false;
    float m_reveal = 0.0F;
    float m_fade = 1.0F;
};

} // namespace coney::gui
