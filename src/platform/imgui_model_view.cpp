// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/imgui_model_view.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <vector>

#include <imgui_internal.h> // ImGuiSettingsHandler
#include <imgui_stdlib.h>

namespace coney::platform {

using debug::ItemKind;
using debug::MenuItem;
using debug::MenuModel;
using debug::MenuPage;

namespace {

// The longest a log box grows before it scrolls, in lines.
constexpr std::size_t kLogLines = 12;
// A plot's height in pixels.
constexpr float kPlotHeight = 60.0F;
// A page window's size the first time it opens, in pixels.
constexpr ImVec2 kWindowSize{440.0F, 520.0F};

// Where a Text item's history walk is: its lines, newest last, and the place in them (-1: the line being typed).
struct HistoryWalk {
    const std::vector<std::string>* lines;
    int* at;
};

// Up and Down in a text box with a history: older and newer lines, back to an empty line past the newest.
int walkHistory(ImGuiInputTextCallbackData* data) {
    auto* walk = static_cast<HistoryWalk*>(data->UserData);
    const auto count = static_cast<int>(walk->lines->size());
    if (count == 0 || data->EventFlag != ImGuiInputTextFlags_CallbackHistory) {
        return 0;
    }
    int& at = *walk->at;
    if (data->EventKey == ImGuiKey_UpArrow) {
        at = at < 0 ? count - 1 : std::max(0, at - 1);
    } else if (data->EventKey == ImGuiKey_DownArrow && at >= 0) {
        at = at + 1 < count ? at + 1 : -1;
    }
    data->DeleteChars(0, data->BufTextLen);
    if (at >= 0) {
        data->InsertChars(0, (*walk->lines)[static_cast<std::size_t>(at)].c_str());
    }
    return 0;
}

// `text` with every % doubled, so it can sit in an ImGui format string.
std::string escapePercent(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out += c;
        if (c == '%') {
            out += '%';
        }
    }
    return out;
}

// Shows `help` as the tooltip of the item just drawn.
void tooltip(const std::string& help) {
    if (!help.empty()) {
        ImGui::SetItemTooltip("%s", help.c_str());
    }
}

} // namespace

void ImGuiModelView::keepOpenPagesInSettings() {
    ImGuiSettingsHandler handler;
    handler.TypeName = "ConeyPages";
    handler.TypeHash = ImHashStr("ConeyPages");
    handler.ReadOpenFn = openSettings;
    handler.ReadLineFn = readSettingsLine;
    handler.WriteAllFn = writeSettings;
    handler.UserData = this;
    ImGui::AddSettingsHandler(&handler);
}

void* ImGuiModelView::openSettings(ImGuiContext* /*context*/, ImGuiSettingsHandler* handler, const char* name) {
    return std::string_view(name) == "Open" ? handler->UserData : nullptr;
}

void ImGuiModelView::readSettingsLine(ImGuiContext* /*context*/, ImGuiSettingsHandler* /*handler*/, void* entry,
                                      const char* line) {
    const std::string_view text(line);
    const auto equals = text.rfind('=');
    if (equals == std::string_view::npos) {
        return;
    }
    static_cast<ImGuiModelView*>(entry)->m_open[std::string(text.substr(0, equals))] = text.substr(equals + 1) == "1";
}

void ImGuiModelView::writeSettings(ImGuiContext* /*context*/, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
    const auto* view = static_cast<const ImGuiModelView*>(handler->UserData);
    out->append("[ConeyPages][Open]\n");
    for (const auto& [title, open] : view->m_open) {
        out->appendf("%s=%d\n", title.c_str(), open ? 1 : 0);
    }
    out->append("\n");
}

bool ImGuiModelView::pageOpen(const std::string& title) const {
    const auto found = m_open.find(title);
    return found != m_open.end() && found->second;
}

void ImGuiModelView::draw(std::string_view hideLabel) {
    // The top-level pages never change after the session is made; Favourites is made again when the pins change.
    if (m_root == nullptr) {
        m_root = m_model.root();
    }
    if (m_model.pins() != m_favouritePins) {
        m_favouritePins = m_model.pins();
        forgetPages(std::string(MenuModel::kFavouritesTitle));
    }

    // The menu bar: a switch per page window.
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Pages")) {
            for (const MenuItem& entry : m_root->items()) {
                if (ImGui::MenuItem(entry.label.c_str(), nullptr, &m_open[entry.label])) {
                    ImGui::MarkIniSettingsDirty();
                }
                tooltip(entry.help);
            }
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("%s", std::string(hideLabel).c_str());
        ImGui::EndMainMenuBar();
    }

    // Each open page as a window: a filter box and a refresh over its items.
    for (const MenuItem& entry : m_root->items()) {
        bool& open = m_open[entry.label];
        if (!open) {
            continue;
        }
        ImGui::SetNextWindowSize(kWindowSize, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(entry.label.c_str(), &open)) {
            ImGuiTextFilter& filter = m_filters[entry.label];
            filter.Draw("Filter", ImGui::GetContentRegionAvail().x * 0.6F);
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh")) {
                forgetPages(entry.label);
            }
            tooltip("Makes the page again, for lists that changed.");
            // Favourites' labels are already full paths.
            const bool favourites = entry.label == MenuModel::kFavouritesTitle;
            if (const MenuPage* page = pageAt(entry, entry.label); page != nullptr) {
                drawPage(*page, favourites ? std::string{} : entry.label, filter.IsActive() ? &filter : nullptr);
            }
        }
        ImGui::End();
        if (!open) {
            forgetPages(entry.label);
            ImGui::MarkIniSettingsDirty();
        }
    }
}

bool ImGuiModelView::drawItem(const MenuItem& item, const std::string& path) { return drawEntry(item, path, nullptr); }

bool ImGuiModelView::drawEntry(const MenuItem& item, const std::string& path, const ImGuiTextFilter* filter) {
    // Each item's widgets are named within its label, so equal widget names on different items never clash.
    ImGui::PushID(item.label.c_str());
    // No default case: a new ItemKind without a renderer here fails the build (-Wswitch, warnings as errors), and
    // a value outside the enum draws nothing.
    bool drawn = static_cast<std::size_t>(item.kind) < debug::kItemKindCount;
    switch (item.kind) {
    case ItemKind::Action:
        if (ImGui::Button(item.label.c_str()) && item.run) {
            item.run();
        }
        tooltip(item.help);
        contextMenu(item, path);
        break;
    case ItemKind::Toggle: {
        bool on = item.getBool && item.getBool();
        if (ImGui::Checkbox(item.label.c_str(), &on) && item.setBool) {
            item.setBool(on);
        }
        tooltip(item.help);
        contextMenu(item, path);
        break;
    }
    case ItemKind::Number: {
        double value = item.getNumber ? item.getNumber() : 0.0;
        std::string format = item.integer ? "%.0f" : "%.3f";
        if (!item.units.empty()) {
            format += " " + escapePercent(item.units);
        }
        // A quarter step per pixel dragged; Ctrl+click types a value, which the range clamps.
        const auto speed = static_cast<float>(item.step * 0.25);
        if (ImGui::DragScalar(item.label.c_str(), ImGuiDataType_Double, &value, speed, &item.min, &item.max,
                              format.c_str(), ImGuiSliderFlags_AlwaysClamp) &&
            item.setNumber) {
            item.setNumber(item.integer ? std::round(value) : value);
        }
        tooltip(item.help);
        contextMenu(item, path);
        break;
    }
    case ItemKind::Choice: {
        const std::size_t chosen = item.getChoice ? item.getChoice() : 0;
        const char* preview = chosen < item.choices.size() ? item.choices[chosen].c_str() : "";
        if (ImGui::BeginCombo(item.label.c_str(), preview)) {
            for (std::size_t i = 0; i < item.choices.size(); ++i) {
                if (ImGui::Selectable(item.choices[i].c_str(), i == chosen) && item.setChoice) {
                    item.setChoice(i);
                }
            }
            ImGui::EndCombo();
        }
        tooltip(item.help);
        contextMenu(item, path);
        break;
    }
    case ItemKind::Text:
        drawText(item, path);
        break;
    case ItemKind::Submenu:
        drawSubmenu(item, path, filter);
        break;
    case ItemKind::Watch:
        drawWatch(item, path);
        break;
    case ItemKind::Log: {
        const std::vector<std::string> lines = item.lines ? item.lines() : std::vector<std::string>{};
        ImGui::TextUnformatted(item.label.c_str());
        tooltip(item.help);
        const std::size_t shown = std::clamp<std::size_t>(lines.size(), 1, kLogLines);
        const float height =
            ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(shown) + ImGui::GetStyle().WindowPadding.y * 2;
        if (ImGui::BeginChild("log", ImVec2(-FLT_MIN, height), ImGuiChildFlags_Borders)) {
            if (lines.empty()) {
                ImGui::TextDisabled("(empty)");
            }
            for (const std::string& line : lines) {
                ImGui::TextUnformatted(line.c_str());
            }
            // Follow new lines while scrolled to the end.
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0F);
            }
        }
        ImGui::EndChild();
        break;
    }
    }
    ImGui::PopID();
    return drawn;
}

void ImGuiModelView::drawPage(const MenuPage& page, const std::string& prefix, const ImGuiTextFilter* filter) {
    for (const MenuItem& item : page.items()) {
        const std::string path = prefix.empty() ? item.label : prefix + "/" + item.label;
        // A submenu decides for itself, by its own label and the items under it.
        if (item.kind != ItemKind::Submenu && filter != nullptr && !filter->PassFilter(item.label.c_str())) {
            continue;
        }
        drawEntry(item, path, filter);
    }
}

void ImGuiModelView::drawSubmenu(const MenuItem& item, const std::string& path, const ImGuiTextFilter* filter) {
    // Under a filter: a submenu whose label matches shows all its items; another shows, opened, only when an item one
    // level down matches (a binding's name in a category of bindings).
    const ImGuiTextFilter* inner = filter;
    if (filter != nullptr) {
        if (filter->PassFilter(item.label.c_str())) {
            inner = nullptr;
        } else {
            const MenuPage* page = pageAt(item, path);
            const bool matches = page != nullptr && std::ranges::any_of(page->items(), [filter](const MenuItem& child) {
                                     return filter->PassFilter(child.label.c_str());
                                 });
            if (!matches) {
                return;
            }
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
    }
    const std::string label = item.detail.empty() ? item.label : std::format("{}  [{}]", item.label, item.detail);
    const bool open = ImGui::TreeNodeEx("node", ImGuiTreeNodeFlags_None, "%s", label.c_str());
    tooltip(item.help);
    contextMenu(item, path);
    if (open) {
        if (const MenuPage* page = pageAt(item, path); page != nullptr) {
            drawPage(*page, path, inner);
        }
        ImGui::TreePop();
    } else if (filter == nullptr) {
        // A closed node's page is made again when it next opens, as the pad menu does, so it is current.
        forgetPages(path);
    }
}

const MenuPage* ImGuiModelView::pageAt(const MenuItem& item, const std::string& path) {
    auto found = m_pages.find(path);
    if (found == m_pages.end()) {
        if (!item.open) {
            return nullptr;
        }
        found = m_pages.emplace(path, item.open()).first;
    }
    return found->second.get();
}

void ImGuiModelView::forgetPages(const std::string& prefix) {
    // The page itself, then every page below it (`prefix/...`), which sort right after it.
    m_pages.erase(prefix);
    const std::string below = prefix + "/";
    auto it = m_pages.lower_bound(below);
    while (it != m_pages.end() && it->first.starts_with(below)) {
        it = m_pages.erase(it);
    }
}

void ImGuiModelView::contextMenu(const MenuItem& item, const std::string& path) {
    if (!ImGui::BeginPopupContextItem("context")) {
        return;
    }
    if (ImGui::MenuItem(m_model.pinned(path) ? "Unpin from Favourites" : "Pin to Favourites")) {
        m_model.togglePin(path);
    }
    if (item.kind == ItemKind::Number && item.defaultValue && ImGui::MenuItem("Reset to default")) {
        debug::resetItem(item);
    }
    ImGui::EndPopup();
}

void ImGuiModelView::drawText(const MenuItem& item, const std::string& path) {
    // The box shows the item's text until it is typed in; a box with a history (a console) starts empty instead and
    // empties again after each line.
    std::string& draft = m_drafts[path];
    if (!m_editing.contains(path) && !item.history && item.getText) {
        draft = item.getText();
    }
    int& historyAt = m_historyAt.try_emplace(path, -1).first->second;
    HistoryWalk walk{item.history.get(), &historyAt};
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (item.history) {
        flags |= ImGuiInputTextFlags_CallbackHistory;
    }
    const bool entered =
        ImGui::InputText(item.label.c_str(), &draft, flags, item.history ? walkHistory : nullptr, &walk);
    if (ImGui::IsItemActive()) {
        m_editing.insert(path);
    } else {
        m_editing.erase(path);
    }
    tooltip(item.help);
    contextMenu(item, path);
    const bool rejected = item.numeric && !draft.empty() && !debug::parseNumber(draft);
    if (rejected) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F), "not a number");
    }
    if (!entered || rejected) {
        return;
    }
    if (item.setText) {
        item.setText(draft);
    }
    m_editing.erase(path);
    if (item.history) {
        draft.clear();
        historyAt = -1;
        // Keep typing in the console after Enter.
        ImGui::SetKeyboardFocusHere(-1);
    }
}

void ImGuiModelView::drawWatch(const MenuItem& item, const std::string& path) {
    const std::string value = item.watch ? item.watch() : std::string{};
    ImGui::LabelText(item.label.c_str(), "%s", value.c_str());
    tooltip(item.help);
    contextMenu(item, path);
    const debug::TimeSeries* series = item.channel.empty() ? nullptr : m_model.channel(item.channel);
    if (series == nullptr) {
        return;
    }
    // Any watch with a channel can be plotted: its last samples, one per simulation step.
    const bool plotted = m_plotted.contains(path);
    if (ImGui::SmallButton(plotted ? "Hide plot" : "Plot")) {
        if (plotted) {
            m_plotted.erase(path);
        } else {
            m_plotted.insert(path);
        }
    }
    if (!plotted) {
        return;
    }
    const std::vector<float> samples = series->values();
    const std::string latest = std::format("{:.3f}", series->latest());
    ImGui::PlotLines("##plot", samples.data(), static_cast<int>(samples.size()), 0, latest.c_str(), FLT_MAX, FLT_MAX,
                     ImVec2(-FLT_MIN, kPlotHeight));
}

} // namespace coney::platform
