// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/menu_navigator.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "core/assert.h"

namespace coney::debug {

namespace {

// The text a Number item starts typing from: its value without units.
std::string numberText(const MenuItem& item) {
    MenuItem bare = item;
    bare.units.clear();
    return valueText(bare);
}

} // namespace

void MenuNavigator::open() {
    if (m_stack.empty()) {
        m_stack.push_back(OpenPage{m_model.root(), 0, 0, {}});
    } else {
        m_stack.back().page->refresh();
    }
    m_open = true;
    clampCursor();
}

void MenuNavigator::close() {
    m_open = false;
    m_edit.reset();
}

void MenuNavigator::setVisibleRows(std::size_t rows) {
    m_visibleRows = std::max<std::size_t>(rows, 1);
    clampCursor();
}

const MenuItem* MenuNavigator::current() const {
    const MenuPage* shown = page();
    if (shown == nullptr || m_stack.back().cursor >= shown->items().size()) {
        return nullptr;
    }
    return &shown->items()[m_stack.back().cursor];
}

std::vector<std::string> MenuNavigator::breadcrumb() const {
    std::vector<std::string> titles;
    titles.reserve(m_stack.size());
    for (const OpenPage& open : m_stack) {
        titles.push_back(open.page->title());
    }
    return titles;
}

std::string MenuNavigator::currentPath() const {
    const MenuItem* item = current();
    if (item == nullptr) {
        return {};
    }
    // On Favourites the label already is the pinned path.
    const std::string& pagePath = m_stack.back().path;
    if (pagePath.empty() || pagePath == MenuModel::kFavouritesTitle) {
        return item->label;
    }
    return pagePath + "/" + item->label;
}

const std::string& MenuNavigator::editText() const {
    static const std::string kEmpty;
    return m_edit ? m_edit->text : kEmpty;
}

void MenuNavigator::say(std::string text) {
    m_status = std::move(text);
    m_statusSteps = kStatusSteps;
}

void MenuNavigator::push(std::shared_ptr<MenuPage> page, const std::string& label) {
    CONEY_ASSERT(page != nullptr);
    const std::string& parent = m_stack.back().path;
    std::string path = parent.empty() ? label : parent + "/" + label;
    const auto remembered = m_remembered.find(path);
    const std::size_t cursor = remembered == m_remembered.end() ? 0 : remembered->second;
    m_stack.push_back(OpenPage{std::move(page), cursor, 0, std::move(path)});
    clampCursor();
}

void MenuNavigator::clampCursor() {
    if (m_stack.empty()) {
        return;
    }
    OpenPage& top = m_stack.back();
    const std::size_t count = top.page->items().size();
    top.cursor = count == 0 ? 0 : std::min(top.cursor, count - 1);
    if (top.cursor < top.scrollTop) {
        top.scrollTop = top.cursor;
    } else if (top.cursor >= top.scrollTop + m_visibleRows) {
        top.scrollTop = top.cursor + 1 - m_visibleRows;
    }
    top.scrollTop = std::min(top.scrollTop, count > m_visibleRows ? count - m_visibleRows : 0);
    m_remembered[top.path] = top.cursor;
}

void MenuNavigator::moveCursor(long long delta, bool wrap) {
    OpenPage& top = m_stack.back();
    const auto count = static_cast<long long>(top.page->items().size());
    if (count == 0) {
        return;
    }
    long long next = static_cast<long long>(top.cursor) + delta;
    if (wrap) {
        next = (next % count + count) % count;
    } else {
        next = std::clamp(next, 0LL, count - 1);
    }
    top.cursor = static_cast<std::size_t>(next);
    clampCursor();
}

void MenuNavigator::apply(const MenuInputFrame& input) {
    if (m_statusSteps > 0) {
        --m_statusSteps;
        if (m_statusSteps == 0) {
            m_status.clear();
        }
    }
    if (!m_open || m_stack.empty() || !input.action) {
        return;
    }
    const MenuItem* item = current();
    if (m_edit && item != nullptr) {
        applyEdit(*input.action, *item);
        return;
    }
    m_edit.reset();
    const auto page = static_cast<long long>(m_visibleRows);
    switch (*input.action) {
    case MenuAction::Up:
        moveCursor(-1, true);
        break;
    case MenuAction::Down:
        moveCursor(1, true);
        break;
    case MenuAction::PageUp:
        moveCursor(-page, false);
        break;
    case MenuAction::PageDown:
        moveCursor(page, false);
        break;
    case MenuAction::Left:
    case MenuAction::Right:
        if (item != nullptr) {
            const int sign = *input.action == MenuAction::Left ? -1 : 1;
            adjustItem(*item, sign * input.multiplier, input.stepSize);
        }
        break;
    case MenuAction::Accept:
        if (item != nullptr) {
            accept(*item);
        }
        break;
    case MenuAction::Back:
        if (m_stack.size() > 1) {
            m_stack.pop_back();
            m_stack.back().page->refresh();
            clampCursor();
        } else {
            close();
        }
        break;
    case MenuAction::Pin:
        if (item != nullptr && item->kind != ItemKind::Log) {
            const bool pinned = m_model.togglePin(currentPath());
            say(pinned ? "Pinned to Favourites" : "Unpinned");
            if (m_stack.back().path == MenuModel::kFavouritesTitle) {
                m_stack.back().page = m_model.favourites();
                clampCursor();
            }
        }
        break;
    case MenuAction::Reset:
        if (item != nullptr && resetItem(*item)) {
            say("Reset to default");
        }
        break;
    }
}

void MenuNavigator::accept(const MenuItem& item) {
    switch (item.kind) {
    case ItemKind::Action:
        if (item.run) {
            item.run();
        }
        break;
    case ItemKind::Toggle:
    case ItemKind::Choice:
        adjustItem(item, 1, StepSize::Normal);
        break;
    case ItemKind::Number:
    case ItemKind::Text:
        startEdit(item);
        break;
    case ItemKind::Submenu:
        if (item.open) {
            if (std::shared_ptr<MenuPage> next = item.open(); next != nullptr) {
                push(std::move(next), item.label);
            }
        }
        break;
    case ItemKind::Watch:
    case ItemKind::Log:
        break;
    }
}

void MenuNavigator::startEdit(const MenuItem& item) {
    Edit edit;
    edit.numeric = item.kind == ItemKind::Number || item.numeric;
    edit.text = item.kind == ItemKind::Number ? numberText(item) : valueText(item);
    if (edit.text.empty()) {
        edit.text = edit.numeric ? "0" : " ";
    }
    edit.cursor = edit.text.size() - 1;
    edit.historyIndex = item.history ? item.history->size() : 0;
    m_edit = std::move(edit);
}

void MenuNavigator::applyEdit(MenuAction action, const MenuItem& item) {
    Edit& edit = *m_edit;
    const std::string_view characters = edit.numeric ? kNumberCharacters : kTextCharacters;
    const std::size_t maxLength = item.kind == ItemKind::Text ? item.maxLength : 24;
    switch (action) {
    case MenuAction::Up:
    case MenuAction::Down: {
        // Turn the character under the cursor through the set; one not in the set starts from its first.
        const std::size_t at = characters.find(edit.text[edit.cursor]);
        const std::size_t count = characters.size();
        std::size_t next = 0;
        if (at != std::string_view::npos) {
            next = action == MenuAction::Up ? (at + 1) % count : (at + count - 1) % count;
        }
        edit.text[edit.cursor] = characters[next];
        break;
    }
    case MenuAction::Left:
        edit.cursor = edit.cursor > 0 ? edit.cursor - 1 : 0;
        break;
    case MenuAction::Right:
        if (edit.cursor + 1 < edit.text.size()) {
            ++edit.cursor;
        } else if (edit.text.size() < maxLength) {
            // Past the end: a new character, a copy of the one before so runs of digits or letters are quick.
            edit.text.push_back(edit.text.back());
            ++edit.cursor;
        }
        break;
    case MenuAction::Pin:
        if (edit.text.size() > 1) {
            edit.text.erase(edit.cursor, 1);
            edit.cursor = std::min(edit.cursor, edit.text.size() - 1);
        } else {
            edit.text = edit.numeric ? "0" : " ";
        }
        break;
    case MenuAction::PageUp:
    case MenuAction::PageDown:
        if (item.history && !item.history->empty()) {
            const std::size_t size = item.history->size();
            if (action == MenuAction::PageUp && edit.historyIndex > 0) {
                --edit.historyIndex;
            } else if (action == MenuAction::PageDown && edit.historyIndex < size) {
                ++edit.historyIndex;
            }
            edit.text = edit.historyIndex < size ? (*item.history)[edit.historyIndex] : " ";
            edit.cursor = edit.text.empty() ? 0 : edit.text.size() - 1;
            if (edit.text.empty()) {
                edit.text = " ";
            }
        }
        break;
    case MenuAction::Accept:
        commitEdit(item);
        break;
    case MenuAction::Back:
        m_edit.reset();
        say("Cancelled");
        break;
    case MenuAction::Reset:
        break;
    }
}

void MenuNavigator::commitEdit(const MenuItem& item) {
    if (!m_edit) {
        return;
    }
    // Spaces at the ends are the spinner's blanks, not text.
    std::string text = m_edit->text;
    const auto first = text.find_first_not_of(' ');
    text = first == std::string::npos ? std::string{} : text.substr(first, text.find_last_not_of(' ') - first + 1);
    if (item.kind == ItemKind::Number) {
        const std::optional<double> number = parseNumber(text);
        if (!number || !item.setNumber) {
            say("Not a number");
            return;
        }
        item.setNumber(std::clamp(item.integer ? std::round(*number) : *number, item.min, item.max));
    } else {
        if (item.numeric && !parseNumber(text)) {
            say("Not a number");
            return;
        }
        if (item.setText) {
            item.setText(text);
        }
    }
    m_edit.reset();
}

} // namespace coney::debug
