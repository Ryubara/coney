// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gui/menu_input.h"
#include "gui/mod_ui/ui_types.h"
#include "gui/option_grid.h"
#include "gui/profile_management_gui/pm_new_game_screens.h"
#include "gui/scrolling_menu.h"

namespace coney::gui {

/// One widget of a mod screen: its definition and state, and the game's own class that holds its logic for the kinds
/// that take input (a grid's OptionGrid, a list's ScrollingMenu, a keyboard's NameKeyboard). UiView draws it.
struct UiWidget {
    UiWidgetId id{};
    UiWidgetDef def{};
    bool visible = true;
    float fill = 0.0F;                        ///< A bar's fill, 0 to 1.
    std::vector<UiItem> items{};              ///< A grid's or list's items.
    std::unique_ptr<OptionGrid> grid{};       ///< A grid's logic; it draws too. Pointers: a widget cannot move.
    std::unique_ptr<ScrollingMenu> list{};    ///< A list's cursor and window.
    std::unique_ptr<NameKeyboard> keyboard{}; ///< A keyboard's cells and cursor.
    std::string typed{};                      ///< What the keyboard has taken so far.

    /// Whether the widget can take the screen's focus: a visible grid, list or keyboard.
    [[nodiscard]] bool focusable() const;
};

/// A choice dialog open on a screen (`Coney.AskChoice`): the message box's choice laid out by UiView, its one-row grid
/// the game's OptionGrid.
struct UiDialog {
    std::string text{};
    std::unique_ptr<OptionGrid> choices{}; ///< One row; a pointer, a grid cannot move.
};

/// The front-end cues a mod screen plays (docs/research/frontend.md#audio-cues-references-only).
namespace ui_cue {
/// A grid's or list's move (their default); for focus moving to another widget, Coney's choice.
inline constexpr int kMove = 4;
/// An accept on a grid, list or dialog: the message box's cue, Coney's choice for the grid and list.
inline constexpr int kAccept = 8;
inline constexpr int kKeyMove = 7;     ///< A keyboard move (PM_Create's).
inline constexpr int kCharacter = 0xa; ///< A character typed.
inline constexpr int kOk = 0xb;        ///< The keyboard's OK on a text.
inline constexpr int kDelete = 0xc;    ///< The keyboard's DEL.
inline constexpr int kRefused = 0xe;   ///< A refused move or entry.
inline constexpr int kBack = 0xf;      ///< Back.
} // namespace ui_cue

/// One screen of the UI model: its widgets in the order they were added, which of them has the focus, and an open
/// choice dialog. It turns one menu command into widget moves, focus moves and events.
///
/// - **Commands reach the focused widget** as the game's widgets take them: a grid through OptionGrid::handle, a list
///   through ScrollingMenu::handle, a keyboard through NameKeyboard::move, with their cues and refusals.
/// - **Between widgets**: a direction that would take the focused widget past its edge (a grid's first or last row or
///   column, a list's first or last entry and its left and right, a keyboard's move that cannot leave its cell) moves
///   the focus instead, when there is somewhere to go: the neighbour the widget names for that direction, else the next
///   focusable widget in the order they were added (down and right) or the previous one (up and left). With nowhere to
///   go the widget takes the command as it would alone, so a screen of one widget moves exactly as the game's widget
///   does. A focus move (Coney's rule, never wrapping from the last widget to the first) takes the focus as the game's
///   widgets do (the input forgets its last command and blocks the d-pad for MenuInput::kFocusBlockMs) and plays
///   ui_cue::kMove.
/// - **A dialog** takes every command while open; back ends it with no choice.
///
/// Back itself is the model's (UiModel::step()): back always closes.
///
/// Presentation state: each machine's own; ids, never pointers, name it from outside.
///
/// Research: docs/guides/studio/ui.md#focus
/// @coney-only the UI widget model for mods; the original's screens are fixed in code
class UiScreen {
  public:
    /// The labels the keyboard shows (global strings 0x97, 0x99 and 0x9a on the front end).
    struct KeyboardStrings {
        std::string characters{};
        std::string ok{};
        std::string del{};
    };

    /// A screen `id` of `project`, sending its cues to `playCue` (may be empty).
    /// @coney-only the UI widget model for mods
    UiScreen(UiScreenId id, std::string project, UiScreenDef def, std::function<void(int cue)> playCue);
    ~UiScreen() = default;
    UiScreen(const UiScreen&) = delete;
    UiScreen& operator=(const UiScreen&) = delete;
    UiScreen(UiScreen&&) = delete;
    UiScreen& operator=(UiScreen&&) = delete;

    /// Adds widget `id` from `def`; a keyboard takes `keys`. Refuses a name another widget has and a text over
    /// ui_limit::kTextChars.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> addWidget(UiWidgetId id, UiWidgetDef def, const KeyboardStrings& keys);
    /// Appends `item` to grid or list `id`. Refuses another kind, a list past ui_limit::kListItems and a grid past
    /// OptionGrid::kMaxItems.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> addItem(UiWidgetId id, UiItem item);
    /// Removes widget `id`; the focus passes to the first focusable widget left. False when it is not here.
    /// @coney-only the UI widget model for mods
    bool removeWidget(UiWidgetId id, std::uint64_t nowMs, MenuInput& input);

    /// Shows or hides widget `id`; hiding the focused widget passes the focus to the first focusable widget left. False
    /// when it is not here.
    /// @coney-only the UI widget model for mods
    bool setVisible(UiWidgetId id, bool visible, std::uint64_t nowMs, MenuInput& input);
    /// Gives `id` the focus at `nowMs` through `input`.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> focus(UiWidgetId id, std::uint64_t nowMs, MenuInput& input);
    /// Gives the focus back to the focused widget, or to the first focusable one: the screen came on top.
    /// @coney-only the UI widget model for mods
    void takeFocus(std::uint64_t nowMs, MenuInput& input);

    /// Opens a choice dialog: `text` over `choices` (one or two), `defaultChoice` selected, taking the focus.
    /// @coney-only the UI widget model for mods
    void openDialog(std::string text, const std::vector<std::string>& choices, std::size_t defaultChoice,
                    std::uint64_t nowMs, MenuInput& input);

    /// Acts on one command other than back at `nowMs`, appending what happened to `events`.
    /// @coney-only the UI widget model for mods
    void handle(MenuCommand command, std::uint64_t nowMs, MenuInput& input, std::vector<UiEvent>& events);
    /// Back on this screen: ends an open dialog (true, with a Choice event of no item), or false when there is none
    /// and the screen itself must close.
    /// @coney-only the UI widget model for mods
    bool backDialog(std::vector<UiEvent>& events);

    /// The widget `id`, or null.
    [[nodiscard]] UiWidget* widget(UiWidgetId id);
    /// The widget `id`, or null.
    [[nodiscard]] const UiWidget* widget(UiWidgetId id) const;
    /// The widget named `name`, or null.
    [[nodiscard]] const UiWidget* find(std::string_view name) const;

    /// The screen's id.
    [[nodiscard]] UiScreenId id() const { return m_id; }
    /// The project the screen belongs to.
    [[nodiscard]] const std::string& project() const { return m_project; }
    /// How it was made.
    [[nodiscard]] const UiScreenDef& def() const { return m_def; }
    /// Its widgets in the order they were added.
    [[nodiscard]] const std::vector<std::unique_ptr<UiWidget>>& widgets() const { return m_widgets; }
    /// The focused widget, if any.
    [[nodiscard]] std::optional<UiWidgetId> focused() const { return m_focus; }
    /// The open dialog, or null.
    [[nodiscard]] const UiDialog* dialog() const { return m_dialog ? &*m_dialog : nullptr; }

  private:
    // The index of widget `id` in m_widgets, if it is here.
    [[nodiscard]] std::optional<std::size_t> indexOf(UiWidgetId id) const;
    // Whether `command` would take widget `w` past its edge.
    [[nodiscard]] static bool atEdge(const UiWidget& w, MenuCommand command);
    // Where the focus goes from widget `from` for `command`: its named neighbour, else the next or previous focusable.
    [[nodiscard]] std::optional<std::size_t> focusTarget(std::size_t from, MenuCommand command) const;
    // Moves the focus to the widget at `index`: its class takes the focus through `input`.
    void moveFocus(std::size_t index, std::uint64_t nowMs, MenuInput& input);
    // A command for the focused widget, as its class takes it.
    void widgetCommand(UiWidget& w, MenuCommand command, std::uint64_t nowMs, std::vector<UiEvent>& events);
    // The keyboard's accept: a character, DEL or OK.
    void keyboardAccept(UiWidget& w, std::vector<UiEvent>& events);
    // Plays `cue`, if the screen has a way to.
    void playCue(int cue) const;

    UiScreenId m_id;
    std::string m_project;
    UiScreenDef m_def;
    std::function<void(int cue)> m_playCue;
    std::vector<std::unique_ptr<UiWidget>> m_widgets;
    std::optional<UiWidgetId> m_focus;
    std::optional<UiDialog> m_dialog;
};

} // namespace coney::gui
