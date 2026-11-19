// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/mod_ui/ui_model.h"

#include <utility>

namespace coney::gui {

UiModel::UiModel(std::function<void(int cue)> playCue) : m_playCue(std::move(playCue)) {}
void UiModel::registerProject(std::string, std::string) {}
std::string_view UiModel::projectTitle(std::string_view) const { return {}; }
std::expected<UiScreenId, UiRefusal> UiModel::createScreen(std::string_view, UiScreenDef) {
    return std::unexpected(UiRefusal::UnknownProject);
}
std::expected<void, UiRefusal> UiModel::destroyScreen(std::string_view, UiScreenId, std::uint64_t) {
    return std::unexpected(UiRefusal::UnknownScreen);
}
std::expected<UiWidgetId, UiRefusal> UiModel::addWidget(std::string_view, UiScreenId, UiWidgetDef) {
    return std::unexpected(UiRefusal::UnknownScreen);
}
std::expected<void, UiRefusal> UiModel::addItem(std::string_view, UiWidgetId, UiItem) {
    return std::unexpected(UiRefusal::UnknownWidget);
}
std::expected<void, UiRefusal> UiModel::removeWidget(std::string_view, UiWidgetId, std::uint64_t) {
    return std::unexpected(UiRefusal::UnknownWidget);
}
std::expected<void, UiRefusal> UiModel::setText(std::string_view, UiWidgetId, std::string) {
    return std::unexpected(UiRefusal::UnknownWidget);
}
std::expected<void, UiRefusal> UiModel::setVisible(std::string_view, UiWidgetId, bool, std::uint64_t) {
    return std::unexpected(UiRefusal::UnknownWidget);
}
std::expected<void, UiRefusal> UiModel::setPosition(std::string_view, UiWidgetId, float, float) {
    return std::unexpected(UiRefusal::UnknownWidget);
}
std::expected<void, UiRefusal> UiModel::setFill(std::string_view, UiWidgetId, float) {
    return std::unexpected(UiRefusal::UnknownWidget);
}
std::expected<void, UiRefusal> UiModel::focus(std::string_view, UiWidgetId, std::uint64_t) {
    return std::unexpected(UiRefusal::UnknownWidget);
}
std::expected<void, UiRefusal> UiModel::askChoice(std::string_view, UiScreenId, std::string,
                                                  const std::vector<std::string>&, std::size_t, std::uint64_t) {
    return std::unexpected(UiRefusal::UnknownScreen);
}
std::expected<void, UiRefusal> UiModel::openScreen(std::string_view, UiScreenId, std::size_t, std::uint64_t) {
    return std::unexpected(UiRefusal::UnknownScreen);
}
std::expected<void, UiRefusal> UiModel::closeScreen(std::string_view, UiScreenId, std::uint64_t) {
    return std::unexpected(UiRefusal::UnknownScreen);
}
void UiModel::openTrusted() {}
void UiModel::closeTrusted(std::uint64_t) {}
void UiModel::step(std::size_t, const Pad&, std::uint64_t) {}
std::vector<UiEvent> UiModel::takeEvents() { return {}; }
bool UiModel::takesPad(std::size_t) const { return false; }
bool UiModel::pausesPlay(std::size_t) const { return false; }
const std::vector<UiScreenId>& UiModel::flow(std::size_t slot) const { return m_flows.at(slot); }
std::optional<UiScreenId> UiModel::top(std::size_t) const { return std::nullopt; }
const UiScreen* UiModel::screen(UiScreenId) const { return nullptr; }
std::optional<UiScreenId> UiModel::screenOf(UiWidgetId) const { return std::nullopt; }

} // namespace coney::gui
