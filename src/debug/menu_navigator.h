// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "debug/menu_model.h"
#include "debug/pad_menu_input.h"

namespace coney::debug {

/// The pad menu's state over a MenuModel: which pages are open (the breadcrumb), the cursor and scroll of each, the
/// cursor remembered for every page visited, and the text being typed with the pad's spinners. It applies a step's
/// MenuAction to the model through the item callbacks; it draws nothing (src/gui/debug_menu_view.h does).
///
/// Typing with a pad (a Text item, or a Number typed exactly): cross starts it with the item's text; up and down
/// turn the character under the cursor through kTextCharacters (kNumberCharacters for numbers); left and right move
/// the cursor, right past the end adds a character; square deletes the character; L1 and R1 recall older and newer
/// lines of the item's history; cross enters the text, circle cancels.
class MenuNavigator {
  public:
    /// The characters the spinner turns through when typing text, space first.
    static constexpr std::string_view kTextCharacters =
        " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.,:;=()'\"+-*/<>[]{}#!?~";
    /// The characters the spinner turns through when typing a number.
    static constexpr std::string_view kNumberCharacters = "0123456789.-e";
    /// Steps a status line stays: 2 s.
    static constexpr int kStatusSteps = 60;

    /// A navigator over `model`, which must outlive it. Closed, with no page open.
    explicit MenuNavigator(MenuModel& model) : m_model(model) {}

    /// Opens the menu: on the root page the first time, otherwise where it was closed (that page refreshed).
    void open();
    /// Closes the menu, ending any typing; the pages stay for the next open().
    void close();
    /// Whether the menu is open.
    [[nodiscard]] bool isOpen() const { return m_open; }

    /// Applies one step's input; does nothing while closed. Counts down the status line.
    void apply(const MenuInputFrame& input);

    /// How many items the view shows at once; the cursor is kept inside that window. At least 1.
    void setVisibleRows(std::size_t rows);
    /// How many items the view shows at once.
    [[nodiscard]] std::size_t visibleRows() const { return m_visibleRows; }

    /// The page on top (the one shown), or null while nothing is open.
    [[nodiscard]] const MenuPage* page() const { return m_stack.empty() ? nullptr : m_stack.back().page.get(); }
    /// The item under the cursor, or null.
    [[nodiscard]] const MenuItem* current() const;
    /// The cursor's index on the shown page.
    [[nodiscard]] std::size_t cursor() const { return m_stack.empty() ? 0 : m_stack.back().cursor; }
    /// The first item shown.
    [[nodiscard]] std::size_t scrollTop() const { return m_stack.empty() ? 0 : m_stack.back().scrollTop; }
    /// The titles of the open pages, root first.
    [[nodiscard]] std::vector<std::string> breadcrumb() const;
    /// The path of the item under the cursor, as pins name it (MenuModel::togglePin()); empty when none.
    [[nodiscard]] std::string currentPath() const;

    /// Whether text is being typed into the item under the cursor.
    [[nodiscard]] bool editing() const { return m_edit.has_value(); }
    /// The text being typed; empty when not typing.
    [[nodiscard]] const std::string& editText() const;
    /// The position of the typing cursor in editText().
    [[nodiscard]] std::size_t editCursor() const { return m_edit ? m_edit->cursor : 0; }

    /// The last action's message (`Pinned`, `Not a number`), for kStatusSteps steps; empty otherwise.
    [[nodiscard]] const std::string& status() const { return m_status; }

  private:
    // One open page: the page, the cursor, the first row shown, and the labels that lead to it (for pins).
    struct OpenPage {
        std::shared_ptr<MenuPage> page;
        std::size_t cursor = 0;
        std::size_t scrollTop = 0;
        std::string path; // submenu labels from the root, joined by '/'; empty for the root
    };
    // Typing in progress.
    struct Edit {
        std::string text;
        std::size_t cursor = 0;
        bool numeric = false;
        std::size_t historyIndex = 0; // history size when not recalling
    };

    // Opens `page`, reached through the submenu labelled `label`, with its remembered cursor.
    void push(std::shared_ptr<MenuPage> page, const std::string& label);
    // Moves the cursor by `delta` items, wrapping at the ends when `wrap`.
    void moveCursor(long long delta, bool wrap);
    // Keeps the cursor inside the page and the shown window; remembers it.
    void clampCursor();
    // Accept on the item under the cursor.
    void accept(const MenuItem& item);
    // Starts typing into `item`.
    void startEdit(const MenuItem& item);
    // Applies one action while typing.
    void applyEdit(MenuAction action, const MenuItem& item);
    // Ends typing by entering the text; stays in typing when the text is refused.
    void commitEdit(const MenuItem& item);
    // Shows `text` as the status line.
    void say(std::string text);

    MenuModel& m_model;
    bool m_open = false;
    std::vector<OpenPage> m_stack;
    std::map<std::string, std::size_t> m_remembered; // page path -> cursor
    std::size_t m_visibleRows = 16;
    std::optional<Edit> m_edit;
    std::string m_status;
    int m_statusSteps = 0;
};

} // namespace coney::debug
