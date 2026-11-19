// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods: the ids, definitions, events and limits it shares; the original has no
// UI a script can build (docs/guides/studio/ui.md#original-natives)
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "graphics/render_device.h"
#include "gui/colour_table.h"
#include "gui/text_layout.h"

namespace coney::gui {

/// A screen of the UI model, by id: stable for the screen's life and never reused in a session, so another machine,
/// a replay or a test can name it.
enum class UiScreenId : std::uint32_t {};
/// A widget of the UI model, by id, as UiScreenId.
enum class UiWidgetId : std::uint32_t {};

/// The player slots a mod screen can be opened for: one per controller port.
inline constexpr std::size_t kUiSlots = 2;

/// The per-project bounds the model enforces with an error (docs/guides/studio/ui.md#limits): first guesses, to be
/// measured on real projects.
namespace ui_limit {
inline constexpr std::size_t kScreens = 16;     ///< Screens alive at once.
inline constexpr std::size_t kWidgets = 512;    ///< Widgets alive at once, over all its screens.
inline constexpr std::size_t kHudShown = 4;     ///< HUD screens shown at once.
inline constexpr std::size_t kTextChars = 2048; ///< Bytes of one text.
inline constexpr std::size_t kListItems = 100;  ///< Items of one list.
inline constexpr std::size_t kAskText = 32;     ///< Characters the keyboard takes (docs/guides/studio/ui.md#safety).
} // namespace ui_limit

/// Where a screen shows: a `Menu` screen takes its player's pad, a `Hud` one never does and draws with the HUD.
enum class UiLayer : std::uint8_t { Menu, Hud };

/// How a screen is made (`Coney.CreateScreen`'s definition).
struct UiScreenDef {
    UiLayer layer = UiLayer::Menu;
    std::string title{}; ///< The screen's own title (the frame label shows the project's, never this).
    bool pause = false;  ///< Offline, play is suspended under it (a mode over gameplay) instead of going on.
};

/// The kinds of widget a mod screen is built of: the game's own classes (docs/guides/studio/ui.md#coney-natives).
enum class UiWidgetKind : std::uint8_t {
    Text,     ///< TextWidget: a text with the game's markup.
    Sprite,   ///< BaseWidget: a rectangle of a sprite sheet.
    Grid,     ///< OptionGrid: items in rows; focusable.
    List,     ///< ScrollingMenu: a window of entries; focusable.
    Bar,      ///< Bar: a meter filled 0 to 1.
    Header,   ///< CircledTextHeader: the pause and statistics screens' circled title.
    Usage,    ///< UsageInfo: the usage line with button glyphs.
    Message,  ///< MessageBox: a timed message in the box's place and style.
    Keyboard, ///< NameKeyboard: the profile manager's pad keyboard; focusable.
};

/// The directions a focusable widget can name a neighbour for.
enum class UiDirection : std::uint8_t { Up, Down, Left, Right };

/// The neighbours a widget names, by the widget names of its screen; empty: the order the widgets were added in.
struct UiNeighbours {
    std::string up{};
    std::string down{};
    std::string left{};
    std::string right{};
};

/// One item of a grid or a list (`Coney.AddItem`).
struct UiItem {
    std::string text{};
    std::string value{}; ///< What a handler is given with the item; the project's own.
    bool enabled = true; ///< A grid skips a disabled item.
};

/// How a widget is made (`Coney.Add*`'s definition). Fields a kind does not use are ignored.
struct UiWidgetDef {
    UiWidgetKind kind = UiWidgetKind::Text;
    std::string name{}; ///< The project's id for the widget, unique in its screen; may be empty.
    float x = 0.5F;     ///< GUI x (0 to 1 inside the safe area).
    float y = 0.5F;     ///< GUI y, down.
    float size = 1.0F;  ///< The font scale, or a sprite's or bar's height in overlay units.
    float width = 0.0F; ///< A bar's width, a text's wrap width; 0: the kind's default.
    std::string text{}; ///< A text's, header's, usage line's or message's text.
    int fontSlot = kTextFontSlot;
    graphics::Rgba colour = kMenuGrey;
    TextAlignment alignment = TextAlignment::Left;
    std::string sheet{};             ///< A sprite's, bar's or header's sprite sheet (a `game:/` sheet name).
    std::size_t rect = 0;            ///< The sheet's rectangle.
    std::vector<std::size_t> rows{}; ///< A grid's items per row.
    std::size_t visibleRows = 3;     ///< A list's window.
    std::uint64_t durationMs = 0;    ///< A message's time.
    UiNeighbours neighbours{};
};

/// What a screen tells its project (`Coney.OnWidget`'s events), queued in order and taken by the host.
enum class UiEventKind : std::uint8_t {
    Accept, ///< A grid or list item was chosen (`item`).
    Move,   ///< The selection of a grid or list changed (`item`).
    Focus,  ///< Focus moved to `widget`.
    Back,   ///< Back was pressed: the screen closes after it, whatever the handler does.
    Closed, ///< The screen was closed (by back, by the project, or by a trusted screen's rule).
    Choice, ///< A choice dialog ended: `item` is the choice, none when back closed it.
    Text,   ///< The keyboard's OK: `text` is what was typed.
};

/// One event of a screen.
struct UiEvent {
    UiEventKind kind = UiEventKind::Accept;
    UiScreenId screen{};
    std::optional<UiWidgetId> widget{};
    std::optional<std::size_t> item{};
    std::string text{};
};

/// Why the model refused a call: the natives turn it into an error the script can test.
enum class UiRefusal : std::uint8_t {
    UnknownProject, ///< The project was never registered by Coney.
    UnknownScreen,  ///< No such screen (or not the project's).
    UnknownWidget,  ///< No such widget (or not the project's).
    TooManyScreens, ///< ui_limit::kScreens.
    TooManyWidgets, ///< ui_limit::kWidgets.
    TooManyHud,     ///< ui_limit::kHudShown.
    TextTooLong,    ///< ui_limit::kTextChars.
    TooManyItems,   ///< ui_limit::kListItems, or a grid's OptionGrid::kMaxItems.
    NameTaken,      ///< Another widget of the screen has the name.
    NotFocusable,   ///< Focus asked for a widget that cannot take it.
    WrongKind,      ///< The call does not apply to the widget's kind.
    TrustedOpen,    ///< A trusted screen is open: no mod screen opens.
    SlotTaken,      ///< Another project's screen is open for the slot.
    BadSlot,        ///< The slot is not one of kUiSlots.
    AlreadyOpen,    ///< The screen is already open.
};

} // namespace coney::gui
