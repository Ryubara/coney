// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#include "gui/mod_ui/ui_screen.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "gui/message_box.h"

namespace coney::gui {

namespace {

// The direction of a menu command; none for accept and back.
std::optional<UiDirection> directionOf(MenuCommand command) {
    switch (command) {
    case MenuCommand::Up:
        return UiDirection::Up;
    case MenuCommand::Down:
        return UiDirection::Down;
    case MenuCommand::Left:
        return UiDirection::Left;
    case MenuCommand::Right:
        return UiDirection::Right;
    case MenuCommand::Accept:
    case MenuCommand::Back:
        break;
    }
    return std::nullopt;
}

// The neighbour a widget names for `direction`; empty for none.
const std::string& neighbourFor(const UiNeighbours& neighbours, UiDirection direction) {
    switch (direction) {
    case UiDirection::Up:
        return neighbours.up;
    case UiDirection::Down:
        return neighbours.down;
    case UiDirection::Left:
        return neighbours.left;
    case UiDirection::Right:
        break;
    }
    return neighbours.right;
}

// Whether a text of the kind is held to ui_limit::kTextChars: every kind that shows `text`.
bool tooLong(const std::string& text) { return text.size() > ui_limit::kTextChars; }

} // namespace

bool UiWidget::focusable() const {
    return visible &&
           (def.kind == UiWidgetKind::Grid || def.kind == UiWidgetKind::List || def.kind == UiWidgetKind::Keyboard);
}

UiScreen::UiScreen(UiScreenId id, std::string project, UiScreenDef def, std::function<void(int cue)> playCue)
    : m_id(id), m_project(std::move(project)), m_def(std::move(def)), m_playCue(std::move(playCue)) {}

std::expected<void, UiRefusal> UiScreen::addWidget(UiWidgetId id, UiWidgetDef def, const KeyboardStrings& keys) {
    if (tooLong(def.text)) {
        return std::unexpected(UiRefusal::TextTooLong);
    }
    if (!def.name.empty() && find(def.name) != nullptr) {
        return std::unexpected(UiRefusal::NameTaken);
    }
    auto w = std::make_unique<UiWidget>();
    w->id = id;
    // The game's class that holds the kind's logic, its cues sent through the screen.
    switch (def.kind) {
    case UiWidgetKind::Grid: {
        w->grid = std::make_unique<OptionGrid>();
        OptionGridSetup setup;
        setup.y = def.y;
        setup.rows = def.rows;
        setup.centreX = def.x;
        setup.moveCue = ui_cue::kMove;
        setup.playCue = [this](int cue) { playCue(cue); };
        w->grid->setup(std::move(setup));
        break;
    }
    case UiWidgetKind::List:
        w->list = std::make_unique<ScrollingMenu>();
        w->list->setup(ScrollingMenuSetup{.visible = std::max<std::size_t>(def.visibleRows, 1),
                                          .moveCue = ui_cue::kMove,
                                          .playCue = [this](int cue) { playCue(cue); }},
                       0);
        break;
    case UiWidgetKind::Keyboard:
        w->keyboard = std::make_unique<NameKeyboard>();
        w->keyboard->set(def.text.empty() ? keys.characters : def.text, keys.ok, keys.del);
        break;
    case UiWidgetKind::Text:
    case UiWidgetKind::Sprite:
    case UiWidgetKind::Bar:
    case UiWidgetKind::Header:
    case UiWidgetKind::Usage:
    case UiWidgetKind::Message:
        break;
    }
    w->def = std::move(def);
    m_widgets.push_back(std::move(w));
    return {};
}

std::expected<void, UiRefusal> UiScreen::addItem(UiWidgetId id, UiItem item) {
    UiWidget* w = widget(id);
    if (w == nullptr) {
        return std::unexpected(UiRefusal::UnknownWidget);
    }
    if (tooLong(item.text)) {
        return std::unexpected(UiRefusal::TextTooLong);
    }
    if (w->grid) {
        // The item's code is its index, which the accept event carries.
        OptionGridItem gridItem;
        gridItem.text = item.text;
        gridItem.code = static_cast<int>(w->items.size());
        gridItem.enabled = item.enabled;
        if (!w->grid->addItem(std::move(gridItem))) {
            return std::unexpected(UiRefusal::TooManyItems);
        }
    } else if (w->list) {
        if (w->items.size() >= ui_limit::kListItems) {
            return std::unexpected(UiRefusal::TooManyItems);
        }
        // The cursor stays where it was over the longer list.
        const std::size_t selected = w->list->selected();
        w->list->setup(ScrollingMenuSetup{.visible = std::max<std::size_t>(w->def.visibleRows, 1),
                                          .moveCue = ui_cue::kMove,
                                          .playCue = [this](int cue) { playCue(cue); }},
                       w->items.size() + 1);
        w->list->select(selected);
    } else {
        return std::unexpected(UiRefusal::WrongKind);
    }
    w->items.push_back(std::move(item));
    return {};
}

bool UiScreen::removeWidget(UiWidgetId id, std::uint64_t nowMs, MenuInput& input) {
    const std::optional<std::size_t> index = indexOf(id);
    if (!index) {
        return false;
    }
    m_widgets.erase(m_widgets.begin() + static_cast<std::ptrdiff_t>(*index));
    if (m_focus == id) {
        m_focus.reset();
        takeFocus(nowMs, input);
    }
    return true;
}

bool UiScreen::setVisible(UiWidgetId id, bool visible, std::uint64_t nowMs, MenuInput& input) {
    UiWidget* w = widget(id);
    if (w == nullptr) {
        return false;
    }
    w->visible = visible;
    if (!visible && m_focus == id) {
        if (w->grid) {
            w->grid->loseFocus();
        }
        m_focus.reset();
        takeFocus(nowMs, input);
    }
    return true;
}

std::expected<void, UiRefusal> UiScreen::focus(UiWidgetId id, std::uint64_t nowMs, MenuInput& input) {
    const std::optional<std::size_t> index = indexOf(id);
    if (!index) {
        return std::unexpected(UiRefusal::UnknownWidget);
    }
    if (!m_widgets.at(*index)->focusable()) {
        return std::unexpected(UiRefusal::NotFocusable);
    }
    moveFocus(*index, nowMs, input);
    return {};
}

void UiScreen::takeFocus(std::uint64_t nowMs, MenuInput& input) {
    // The dialog first, then the widget that had the focus, else the first that can take it.
    if (m_dialog) {
        m_dialog->choices->takeFocus(input, nowMs);
        return;
    }
    std::optional<std::size_t> index;
    if (m_focus) {
        index = indexOf(*m_focus);
        if (index && !m_widgets.at(*index)->focusable()) {
            index.reset();
        }
    }
    for (std::size_t i = 0; !index && i < m_widgets.size(); ++i) {
        if (m_widgets.at(i)->focusable()) {
            index = i;
        }
    }
    if (index) {
        moveFocus(*index, nowMs, input);
    } else {
        // No widget takes it: the input is still cleared, as a screen coming on top clears its record.
        input.focus(nowMs);
        input.setDpadRepeat(true);
        m_focus.reset();
    }
}

void UiScreen::openDialog(std::string text, const std::vector<std::string>& choices, std::size_t defaultChoice,
                          std::uint64_t nowMs, MenuInput& input) {
    // The message box's choice: one row, kDimGrey items at part_page0's size 1.0, the default selected.
    UiDialog dialog;
    dialog.text = std::move(text);
    dialog.choices = std::make_unique<OptionGrid>();
    OptionGridSetup setup;
    setup.y = MessageBox::kChoiceY;
    setup.rows = {std::max<std::size_t>(choices.size(), 1)};
    setup.moveCue = ui_cue::kMove;
    setup.playCue = [this](int cue) { playCue(cue); };
    dialog.choices->setup(std::move(setup));
    for (std::size_t i = 0; i < choices.size(); ++i) {
        OptionGridItem item;
        item.text = choices.at(i);
        item.code = static_cast<int>(i);
        item.colour = kDimGrey;
        item.scale = 1.0F;
        item.fontSlot = kTextFontSlot;
        dialog.choices->addItem(std::move(item));
    }
    if (dialog.choices->items() > 0) {
        dialog.choices->select(std::min(defaultChoice, dialog.choices->items() - 1));
    }
    m_dialog = std::move(dialog);
    m_dialog->choices->takeFocus(input, nowMs);
}

void UiScreen::handle(MenuCommand command, std::uint64_t nowMs, MenuInput& input, std::vector<UiEvent>& events) {
    // A dialog takes every command; accept ends it with the selected choice.
    if (m_dialog) {
        if (const std::optional<int> code = m_dialog->choices->handle(command)) {
            playCue(ui_cue::kAccept);
            events.push_back(UiEvent{.kind = UiEventKind::Choice,
                                     .screen = m_id,
                                     .widget = std::nullopt,
                                     .item = static_cast<std::size_t>(*code),
                                     .text = {}});
            m_dialog.reset();
            takeFocus(nowMs, input);
        }
        return;
    }
    if (!m_focus) {
        return;
    }
    const std::optional<std::size_t> index = indexOf(*m_focus);
    if (!index) {
        return;
    }
    UiWidget& w = *m_widgets.at(*index);
    // A widget that cannot hold the focus (hidden) takes no command.
    if (!w.focusable()) {
        return;
    }
    // A direction past the widget's edge moves the focus when there is somewhere to go.
    if (directionOf(command) && atEdge(w, command)) {
        if (const std::optional<std::size_t> target = focusTarget(*index, command)) {
            moveFocus(*target, nowMs, input);
            playCue(ui_cue::kMove);
            events.push_back(UiEvent{.kind = UiEventKind::Focus,
                                     .screen = m_id,
                                     .widget = m_widgets.at(*target)->id,
                                     .item = std::nullopt,
                                     .text = {}});
            return;
        }
    }
    widgetCommand(w, command, nowMs, events);
}

bool UiScreen::backDialog(std::vector<UiEvent>& events) {
    if (!m_dialog) {
        return false;
    }
    events.push_back(
        UiEvent{.kind = UiEventKind::Choice, .screen = m_id, .widget = std::nullopt, .item = std::nullopt, .text = {}});
    m_dialog.reset();
    return true;
}

UiWidget* UiScreen::widget(UiWidgetId id) {
    const std::optional<std::size_t> index = indexOf(id);
    return index ? m_widgets.at(*index).get() : nullptr;
}

const UiWidget* UiScreen::widget(UiWidgetId id) const {
    const std::optional<std::size_t> index = indexOf(id);
    return index ? m_widgets.at(*index).get() : nullptr;
}

const UiWidget* UiScreen::find(std::string_view name) const {
    for (const std::unique_ptr<UiWidget>& w : m_widgets) {
        if (w->def.name == name) {
            return w.get();
        }
    }
    return nullptr;
}

std::optional<std::size_t> UiScreen::indexOf(UiWidgetId id) const {
    for (std::size_t i = 0; i < m_widgets.size(); ++i) {
        if (m_widgets.at(i)->id == id) {
            return i;
        }
    }
    return std::nullopt;
}

bool UiScreen::atEdge(const UiWidget& w, MenuCommand command) {
    // A grid's edge is its first or last row or column; an empty one is all edge.
    if (w.grid) {
        const OptionGrid& grid = *w.grid;
        if (grid.items() == 0) {
            return true;
        }
        const auto [row, column] = grid.cell(grid.selected());
        std::size_t rowLength = 0;
        for (std::size_t i = 0; i < grid.items(); ++i) {
            if (grid.cell(i).first == row) {
                ++rowLength;
            }
        }
        switch (command) {
        case MenuCommand::Up:
            return row == 0;
        case MenuCommand::Down:
            return row + 1 >= grid.rows();
        case MenuCommand::Left:
            return column == 0;
        case MenuCommand::Right:
            return column + 1 >= rowLength;
        case MenuCommand::Accept:
        case MenuCommand::Back:
            break;
        }
        return false;
    }
    // A list moves only up and down: its first and last entries are its edges, left and right always are.
    if (w.list) {
        const ScrollingMenu& list = *w.list;
        switch (command) {
        case MenuCommand::Up:
            return list.count() == 0 || list.selected() == 0;
        case MenuCommand::Down:
            return list.count() == 0 || list.selected() + 1 >= list.count();
        case MenuCommand::Left:
        case MenuCommand::Right:
            return true;
        case MenuCommand::Accept:
        case MenuCommand::Back:
            break;
        }
        return false;
    }
    // A keyboard's edge is a move that cannot leave its cell, tried on a copy.
    if (w.keyboard) {
        NameKeyboard trial = *w.keyboard;
        return !trial.move(command);
    }
    return true;
}

std::optional<std::size_t> UiScreen::focusTarget(std::size_t from, MenuCommand command) const {
    const std::optional<UiDirection> direction = directionOf(command);
    if (!direction) {
        return std::nullopt;
    }
    // The named neighbour, when it is on the screen and can take the focus.
    const std::string& named = neighbourFor(m_widgets.at(from)->def.neighbours, *direction);
    if (!named.empty()) {
        for (std::size_t i = 0; i < m_widgets.size(); ++i) {
            if (i != from && m_widgets.at(i)->def.name == named && m_widgets.at(i)->focusable()) {
                return i;
            }
        }
    }
    // Else the order the widgets were added in, without wrapping (Coney's choice): forward for down and right, back for
    // up and left.
    if (*direction == UiDirection::Down || *direction == UiDirection::Right) {
        for (std::size_t i = from + 1; i < m_widgets.size(); ++i) {
            if (m_widgets.at(i)->focusable()) {
                return i;
            }
        }
    } else {
        for (std::size_t i = from; i > 0; --i) {
            if (m_widgets.at(i - 1)->focusable()) {
                return i - 1;
            }
        }
    }
    return std::nullopt;
}

void UiScreen::moveFocus(std::size_t index, std::uint64_t nowMs, MenuInput& input) {
    // The old widget gives the focus up; the new one takes it as its class does (OptionGrid_TakeFocus clears the input
    // record and blocks the d-pad; the list and keyboard the same through the input).
    if (m_focus) {
        if (UiWidget* old = widget(*m_focus); old != nullptr && old->grid) {
            old->grid->loseFocus();
        }
    }
    UiWidget& w = *m_widgets.at(index);
    if (w.grid) {
        w.grid->takeFocus(input, nowMs);
    } else if (w.list) {
        w.list->takeFocus(input, nowMs);
    } else {
        input.focus(nowMs);
        input.setDpadRepeat(true);
    }
    m_focus = w.id;
}

void UiScreen::widgetCommand(UiWidget& w, MenuCommand command, std::uint64_t nowMs, std::vector<UiEvent>& events) {
    const auto event = [&](UiEventKind kind, std::optional<std::size_t> item) {
        events.push_back(UiEvent{.kind = kind, .screen = m_id, .widget = w.id, .item = item, .text = {}});
    };
    if (w.grid) {
        const std::size_t before = w.grid->selected();
        if (const std::optional<int> code = w.grid->handle(command)) {
            playCue(ui_cue::kAccept);
            event(UiEventKind::Accept, static_cast<std::size_t>(*code));
        } else if (w.grid->selected() != before) {
            event(UiEventKind::Move, w.grid->selected());
        }
        return;
    }
    if (w.list) {
        const std::size_t before = w.list->selected();
        if (const std::optional<std::size_t> chosen = w.list->handle(command, nowMs)) {
            playCue(ui_cue::kAccept);
            event(UiEventKind::Accept, *chosen);
        } else if (w.list->selected() != before) {
            event(UiEventKind::Move, w.list->selected());
        }
        return;
    }
    if (w.keyboard) {
        if (command == MenuCommand::Accept) {
            keyboardAccept(w, events);
        } else if (w.keyboard->move(command)) {
            playCue(ui_cue::kKeyMove);
        } else {
            playCue(ui_cue::kRefused);
        }
    }
}

void UiScreen::keyboardAccept(UiWidget& w, std::vector<UiEvent>& events) {
    const NameKeyboard::Cell& cell = w.keyboard->cells().at(w.keyboard->selected());
    switch (cell.key) {
    case NameKeyboard::Key::Character:
        // A character adds itself (`_` is a space, as on PM_Create) up to the limit.
        if (w.typed.size() >= ui_limit::kAskText) {
            playCue(ui_cue::kRefused);
            return;
        }
        w.typed += cell.label == "_" ? std::string(" ") : cell.label;
        playCue(ui_cue::kCharacter);
        return;
    case NameKeyboard::Key::Delete:
        if (!w.typed.empty()) {
            w.typed.pop_back();
        }
        playCue(ui_cue::kDelete);
        return;
    case NameKeyboard::Key::Ok:
        break;
    }
    // OK hands the text over; an empty text is refused, as PM_Create refuses an empty name.
    if (w.typed.find_first_not_of(' ') == std::string::npos) {
        playCue(ui_cue::kRefused);
        return;
    }
    playCue(ui_cue::kOk);
    events.push_back(
        UiEvent{.kind = UiEventKind::Text, .screen = m_id, .widget = w.id, .item = std::nullopt, .text = w.typed});
}

void UiScreen::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

} // namespace coney::gui
