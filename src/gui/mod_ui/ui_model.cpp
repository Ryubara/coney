// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#include "gui/mod_ui/ui_model.h"

#include <algorithm>
#include <utility>

namespace coney::gui {

UiModel::UiModel(std::function<void(int cue)> playCue) : m_playCue(std::move(playCue)) {}

void UiModel::registerProject(std::string project, std::string title) {
    m_projects.insert_or_assign(std::move(project), std::move(title));
}

std::string_view UiModel::projectTitle(std::string_view project) const {
    const auto it = m_projects.find(project);
    return it == m_projects.end() ? std::string_view{} : std::string_view{it->second};
}

std::expected<UiScreenId, UiRefusal> UiModel::createScreen(std::string_view project, UiScreenDef def) {
    if (!m_projects.contains(project)) {
        return std::unexpected(UiRefusal::UnknownProject);
    }
    if (screensOf(project) >= ui_limit::kScreens) {
        return std::unexpected(UiRefusal::TooManyScreens);
    }
    if (def.title.size() > ui_limit::kTextChars) {
        return std::unexpected(UiRefusal::TextTooLong);
    }
    const UiScreenId id{m_nextScreen++};
    m_screens.emplace(
        id, std::make_unique<UiScreen>(id, std::string(project), std::move(def), [this](int cue) { playCue(cue); }));
    return id;
}

std::expected<void, UiRefusal> UiModel::destroyScreen(std::string_view project, UiScreenId screen,
                                                      std::uint64_t nowMs) {
    UiScreen* s = owned(project, screen);
    if (s == nullptr) {
        return std::unexpected(UiRefusal::UnknownScreen);
    }
    // Closed first, so the flow and the HUD list let it go and queue Closed; then its widgets and itself.
    if (slotOf(screen) || std::ranges::any_of(m_hud, [&](const auto& shown) { return shown.first == screen; })) {
        (void)closeScreen(project, screen, nowMs);
    }
    for (const std::unique_ptr<UiWidget>& w : s->widgets()) {
        m_widgetScreens.erase(w->id);
    }
    m_screens.erase(screen);
    return {};
}

std::expected<UiWidgetId, UiRefusal> UiModel::addWidget(std::string_view project, UiScreenId screen, UiWidgetDef def) {
    UiScreen* s = owned(project, screen);
    if (s == nullptr) {
        return std::unexpected(UiRefusal::UnknownScreen);
    }
    if (widgetsOf(project) >= ui_limit::kWidgets) {
        return std::unexpected(UiRefusal::TooManyWidgets);
    }
    const UiWidgetId id{m_nextWidget};
    if (std::expected<void, UiRefusal> added = s->addWidget(id, std::move(def), m_keys); !added) {
        return std::unexpected(added.error());
    }
    ++m_nextWidget;
    m_widgetScreens.emplace(id, screen);
    return id;
}

std::expected<void, UiRefusal> UiModel::addItem(std::string_view project, UiWidgetId widget, UiItem item) {
    auto [s, w] = ownedWidget(project, widget);
    if (w == nullptr) {
        return std::unexpected(UiRefusal::UnknownWidget);
    }
    return s->addItem(widget, std::move(item));
}

std::expected<void, UiRefusal> UiModel::removeWidget(std::string_view project, UiWidgetId widget, std::uint64_t nowMs) {
    auto [s, w] = ownedWidget(project, widget);
    if (w == nullptr) {
        return std::unexpected(UiRefusal::UnknownWidget);
    }
    s->removeWidget(widget, nowMs, inputOf(s->id()));
    m_widgetScreens.erase(widget);
    return {};
}

std::expected<void, UiRefusal> UiModel::setText(std::string_view project, UiWidgetId widget, std::string text) {
    auto [s, w] = ownedWidget(project, widget);
    if (w == nullptr) {
        return std::unexpected(UiRefusal::UnknownWidget);
    }
    if (text.size() > ui_limit::kTextChars) {
        return std::unexpected(UiRefusal::TextTooLong);
    }
    w->def.text = std::move(text);
    return {};
}

std::expected<void, UiRefusal> UiModel::setVisible(std::string_view project, UiWidgetId widget, bool visible,
                                                   std::uint64_t nowMs) {
    auto [s, w] = ownedWidget(project, widget);
    if (w == nullptr) {
        return std::unexpected(UiRefusal::UnknownWidget);
    }
    // A hidden widget cannot keep the focus: it passes on as when the widget is removed.
    s->setVisible(widget, visible, nowMs, inputOf(s->id()));
    return {};
}

std::expected<void, UiRefusal> UiModel::setPosition(std::string_view project, UiWidgetId widget, float x, float y) {
    auto [s, w] = ownedWidget(project, widget);
    if (w == nullptr) {
        return std::unexpected(UiRefusal::UnknownWidget);
    }
    w->def.x = x;
    w->def.y = y;
    return {};
}

std::expected<void, UiRefusal> UiModel::setFill(std::string_view project, UiWidgetId widget, float fill) {
    auto [s, w] = ownedWidget(project, widget);
    if (w == nullptr) {
        return std::unexpected(UiRefusal::UnknownWidget);
    }
    if (w->def.kind != UiWidgetKind::Bar) {
        return std::unexpected(UiRefusal::WrongKind);
    }
    w->fill = std::clamp(fill, 0.0F, 1.0F);
    return {};
}

std::expected<void, UiRefusal> UiModel::focus(std::string_view project, UiWidgetId widget, std::uint64_t nowMs) {
    auto [s, w] = ownedWidget(project, widget);
    if (w == nullptr) {
        return std::unexpected(UiRefusal::UnknownWidget);
    }
    return s->focus(widget, nowMs, inputOf(s->id()));
}

std::expected<void, UiRefusal> UiModel::askChoice(std::string_view project, UiScreenId screen, std::string text,
                                                  const std::vector<std::string>& choices, std::size_t defaultChoice,
                                                  std::uint64_t nowMs) {
    UiScreen* s = owned(project, screen);
    if (s == nullptr) {
        return std::unexpected(UiRefusal::UnknownScreen);
    }
    if (s->def().layer != UiLayer::Menu) {
        return std::unexpected(UiRefusal::WrongKind);
    }
    if (text.size() > ui_limit::kTextChars) {
        return std::unexpected(UiRefusal::TextTooLong);
    }
    if (choices.empty() || choices.size() > 2) {
        return std::unexpected(UiRefusal::TooManyItems);
    }
    s->openDialog(std::move(text), choices, defaultChoice, nowMs, inputOf(screen));
    return {};
}

std::expected<void, UiRefusal> UiModel::openScreen(std::string_view project, UiScreenId screen, std::size_t slot,
                                                   std::uint64_t nowMs) {
    UiScreen* s = owned(project, screen);
    if (s == nullptr) {
        return std::unexpected(UiRefusal::UnknownScreen);
    }
    if (slot >= kUiSlots) {
        return std::unexpected(UiRefusal::BadSlot);
    }
    // The trusted layer: nothing of a mod's opens while one of Coney's own screens is up, for any slot (Coney's
    // choice: stricter than the design's rule for the trusted screen's player alone).
    if (trustedOpen()) {
        return std::unexpected(UiRefusal::TrustedOpen);
    }
    if (slotOf(screen) || std::ranges::any_of(m_hud, [&](const auto& shown) { return shown.first == screen; })) {
        return std::unexpected(UiRefusal::AlreadyOpen);
    }
    if (s->def().layer == UiLayer::Hud) {
        const auto shownByProject = std::ranges::count_if(
            m_hud, [&](const auto& shown) { return m_screens.at(shown.first)->project() == project; });
        if (static_cast<std::size_t>(shownByProject) >= ui_limit::kHudShown) {
            return std::unexpected(UiRefusal::TooManyHud);
        }
        m_hud.emplace_back(screen, slot);
        return {};
    }
    // One project per slot: the flow holds its screens only.
    std::vector<UiScreenId>& flow = m_flows.at(slot);
    if (!flow.empty() && m_screens.at(flow.front())->project() != project) {
        return std::unexpected(UiRefusal::SlotTaken);
    }
    flow.push_back(screen);
    s->takeFocus(nowMs, m_inputs.at(slot));
    return {};
}

std::expected<void, UiRefusal> UiModel::closeScreen(std::string_view project, UiScreenId screen, std::uint64_t nowMs) {
    if (owned(project, screen) == nullptr) {
        return std::unexpected(UiRefusal::UnknownScreen);
    }
    if (const std::optional<std::size_t> slot = slotOf(screen)) {
        popTo(*slot, screen, nowMs);
        return {};
    }
    const auto shown = std::ranges::find_if(m_hud, [&](const auto& entry) { return entry.first == screen; });
    if (shown != m_hud.end()) {
        m_hud.erase(shown);
        m_events.push_back(UiEvent{
            .kind = UiEventKind::Closed, .screen = screen, .widget = std::nullopt, .item = std::nullopt, .text = {}});
    }
    return {};
}

void UiModel::openTrusted() { ++m_trusted; }

void UiModel::closeTrusted(std::uint64_t nowMs) {
    if (m_trusted == 0) {
        return;
    }
    --m_trusted;
    // The mod screens under it take their focus again, so no press made on the trusted screen carries over.
    if (m_trusted == 0) {
        for (std::size_t slot = 0; slot < kUiSlots; ++slot) {
            if (const std::optional<UiScreenId> screen = top(slot)) {
                m_screens.at(*screen)->takeFocus(nowMs, m_inputs.at(slot));
            }
        }
    }
}

void UiModel::step(std::size_t slot, const Pad& pad, std::uint64_t nowMs) {
    if (slot >= kUiSlots || trustedOpen()) {
        return;
    }
    const std::optional<UiScreenId> screen = top(slot);
    if (!screen) {
        return;
    }
    MenuInput& input = m_inputs.at(slot);
    UiScreen& s = *m_screens.at(*screen);
    // A screen with no focus but a widget that can take it (one added since it opened) gives it the focus here, so it
    // never stays deaf. Checked once a step rather than in addWidget, which would need the game time on every call.
    if (!s.focused() && s.dialog() == nullptr &&
        std::ranges::any_of(s.widgets(), [](const std::unique_ptr<UiWidget>& w) { return w->focusable(); })) {
        s.takeFocus(nowMs, input);
    }
    const std::optional<MenuCommand> command = input.dispatch(pad, nowMs);
    if (!command) {
        return;
    }
    if (*command != MenuCommand::Back) {
        s.handle(*command, nowMs, input, m_events);
        return;
    }
    // Back always closes: a dialog first, else the screen, whatever its project does with the event.
    playCue(ui_cue::kBack);
    if (s.backDialog(m_events)) {
        s.takeFocus(nowMs, input);
        return;
    }
    m_events.push_back(UiEvent{
        .kind = UiEventKind::Back, .screen = *screen, .widget = std::nullopt, .item = std::nullopt, .text = {}});
    popTo(slot, *screen, nowMs);
}

std::vector<UiEvent> UiModel::takeEvents() { return std::exchange(m_events, {}); }

bool UiModel::takesPad(std::size_t slot) const { return slot < kUiSlots && !m_flows.at(slot).empty(); }

const std::vector<UiScreenId>& UiModel::flow(std::size_t slot) const { return m_flows.at(slot); }

std::optional<UiScreenId> UiModel::top(std::size_t slot) const {
    if (slot >= kUiSlots || m_flows.at(slot).empty()) {
        return std::nullopt;
    }
    return m_flows.at(slot).back();
}

const UiScreen* UiModel::screen(UiScreenId screen) const {
    const auto it = m_screens.find(screen);
    return it == m_screens.end() ? nullptr : it->second.get();
}

std::optional<UiScreenId> UiModel::screenOf(UiWidgetId widget) const {
    const auto it = m_widgetScreens.find(widget);
    return it == m_widgetScreens.end() ? std::nullopt : std::optional<UiScreenId>{it->second};
}

UiScreen* UiModel::owned(std::string_view project, UiScreenId screen) {
    const auto it = m_screens.find(screen);
    if (it == m_screens.end() || it->second->project() != project) {
        return nullptr;
    }
    return it->second.get();
}

std::pair<UiScreen*, UiWidget*> UiModel::ownedWidget(std::string_view project, UiWidgetId widget) {
    const std::optional<UiScreenId> screen = screenOf(widget);
    if (!screen) {
        return {nullptr, nullptr};
    }
    UiScreen* s = owned(project, *screen);
    if (s == nullptr) {
        return {nullptr, nullptr};
    }
    return {s, s->widget(widget)};
}

std::size_t UiModel::screensOf(std::string_view project) const {
    return static_cast<std::size_t>(
        std::ranges::count_if(m_screens, [&](const auto& entry) { return entry.second->project() == project; }));
}

// Sums the widgets of the project's screens: the limit counts them together.
std::size_t UiModel::widgetsOf(std::string_view project) const {
    std::size_t count = 0;
    for (const auto& [id, screen] : m_screens) {
        if (screen->project() == project) {
            count += screen->widgets().size();
        }
    }
    return count;
}

std::optional<std::size_t> UiModel::slotOf(UiScreenId screen) const {
    for (std::size_t slot = 0; slot < kUiSlots; ++slot) {
        if (std::ranges::find(m_flows.at(slot), screen) != m_flows.at(slot).end()) {
            return slot;
        }
    }
    return std::nullopt;
}

void UiModel::popTo(std::size_t slot, UiScreenId screen, std::uint64_t nowMs) {
    // The screens over it go with it, the top first, each with its Closed.
    std::vector<UiScreenId>& flow = m_flows.at(slot);
    while (!flow.empty()) {
        const UiScreenId closed = flow.back();
        flow.pop_back();
        m_events.push_back(UiEvent{
            .kind = UiEventKind::Closed, .screen = closed, .widget = std::nullopt, .item = std::nullopt, .text = {}});
        if (closed == screen) {
            break;
        }
    }
    if (!flow.empty()) {
        m_screens.at(flow.back())->takeFocus(nowMs, m_inputs.at(slot));
    }
}

MenuInput& UiModel::inputOf(UiScreenId screen) {
    const std::optional<std::size_t> slot = slotOf(screen);
    return slot && top(*slot) == screen ? m_inputs.at(*slot) : m_unreadInput;
}

bool UiModel::pausesPlay(std::size_t slot) const {
    if (slot >= kUiSlots) {
        return false;
    }
    return std::ranges::any_of(m_flows.at(slot), [&](UiScreenId id) { return m_screens.at(id)->def().pause; });
}

void UiModel::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

} // namespace coney::gui
