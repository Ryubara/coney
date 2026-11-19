// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/mod_ui/ui_screen.h"

#include <utility>

namespace coney::gui {

bool UiWidget::focusable() const { return false; }

UiScreen::UiScreen(UiScreenId id, std::string project, UiScreenDef def, std::function<void(int cue)> playCue)
    : m_id(id), m_project(std::move(project)), m_def(std::move(def)), m_playCue(std::move(playCue)) {}

std::expected<void, UiRefusal> UiScreen::addWidget(UiWidgetId, UiWidgetDef, const KeyboardStrings&) {
    return std::unexpected(UiRefusal::WrongKind);
}
std::expected<void, UiRefusal> UiScreen::addItem(UiWidgetId, UiItem) { return std::unexpected(UiRefusal::WrongKind); }
bool UiScreen::removeWidget(UiWidgetId, std::uint64_t, MenuInput&) { return false; }
bool UiScreen::setVisible(UiWidgetId, bool, std::uint64_t, MenuInput&) { return false; }
std::expected<void, UiRefusal> UiScreen::focus(UiWidgetId, std::uint64_t, MenuInput&) {
    return std::unexpected(UiRefusal::NotFocusable);
}
void UiScreen::takeFocus(std::uint64_t, MenuInput&) {}
void UiScreen::openDialog(std::string, const std::vector<std::string>&, std::size_t, std::uint64_t, MenuInput&) {}
void UiScreen::handle(MenuCommand, std::uint64_t, MenuInput&, std::vector<UiEvent>&) {}
bool UiScreen::backDialog(std::vector<UiEvent>&) { return false; }
UiWidget* UiScreen::widget(UiWidgetId) { return nullptr; }
const UiWidget* UiScreen::widget(UiWidgetId) const { return nullptr; }
const UiWidget* UiScreen::find(std::string_view) const { return nullptr; }

} // namespace coney::gui
