// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

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
inline constexpr std::uint32_t kRightLabels = 0x132; ///< `0x132`-`0x138`, right-aligned (flags 5).
inline constexpr std::uint32_t kLeftLabels = 0x139;  ///< `0x139`-`0x13f`, left-aligned (flags 4).
inline constexpr std::uint32_t kTipTitles = 0x145;
inline constexpr std::uint32_t kTipTexts = 0x15c;
} // namespace control_strings

/// How many labels each side of the controller picture has, and how many tutorial tips there are.
inline constexpr std::size_t kControllerLabels = 7;
inline constexpr std::size_t kTutorialTips = 23;

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
/// - **Controller** (`ControllerPage`): the title `0x130`, the pad picture (sheet-table record `0x18f` rectangle 0,
///   centred at (0.5, 0.58), 0.51 × 0.45, depth 11,000) and its 14 labels; directions refuse (`0xe`).
/// - **Tutorial** (`TutorialPage`): the title `0x131` and a ScrollingMenu of the 23 tips, 10 visible from the pause
///   menu, clipped to 0.5 high; the focused tip red (134, 26, 26), the others `0x005fd320`; 550 ms after a tip is
///   focused its text shows under it (cue `0xd` as it appears) and the tips below move down.
///
/// Coney's stand-ins where the page gives an address but no value (docs/research/pause.md#open-questions): the
/// pages' title place (the entries' place, which the open page hides), the labels' places (two columns beside the
/// picture), the tips' place, spacing and wrap width, and the text's reveal (shown at once once due).
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
    /// The tutorial tips shown at once from the pause menu, and the list's clip height.
    static constexpr std::size_t kVisibleTips = 10;
    static constexpr float kTipClipHeight = 0.5F;
    /// How long a tip is focused before its text shows (the menu's delay `+0xc4`).
    static constexpr std::uint64_t kDescriptionDelayMs = 550;
    /// The focused tip's colour.
    static constexpr graphics::Rgba kTipFocus{134, 26, 26, 255};

    ControlMenu();

    /// Opens the screen on its entries, Controller selected, no page open: the entries' texts and places, both pages
    /// set up (`ControllerPage_Init`, `TutorialPage_Init`). `strings` and `input` must outlive the menu's use;
    /// `playCue` plays a front-end cue and may be empty.
    /// @orig 0x001e4a30 ControlMenuHUD_Open (ControlMenuHUD.cpp)
    /// @orig 0x001e4130 ControllerPage_Init (ControlMenuHUD.cpp)
    /// @orig 0x001e3828 TutorialPage_Init (ControlMenuHUD.cpp)
    /// @orig 0x001e3520 ControlPage_Init (ControlMenuHUD.cpp)
    void open(const GlobalStrings* strings, MenuInput& input, std::function<void(int cue)> playCue,
              std::uint64_t nowMs);

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
    /// Whether the focused tip's text shows.
    [[nodiscard]] bool descriptionShown() const { return m_descriptionShown; }
    /// The texts shown now, in drawing order (for tests and the log): the entries, or the open page's title, labels
    /// or the tips of the list's window and the shown text (before render() clips the window to kTipClipHeight).
    [[nodiscard]] std::vector<std::string> lines() const;
    /// Entry `index` (0 Controller, 1 Tutorial).
    [[nodiscard]] const TextWidget& entry(std::size_t index) const { return m_entries.at(index); }
    /// The picture widget.
    [[nodiscard]] const BaseWidget& picture() const { return m_picture; }

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
    std::uint64_t m_focusMs = 0;
    std::uint64_t m_nowMs = 0;
    bool m_descriptionShown = false;
    float m_fade = 1.0F;
};

} // namespace coney::gui
