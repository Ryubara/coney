// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/imgui_ui_scale.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string_view>

#include <imgui_internal.h> // the window list, the settings entries

#include "debug/ui_scale.h"

namespace coney::platform {

namespace {

// The font size the overlay was designed for, in points, at scale 1.
constexpr float kBaseFontSize = 13.0F;

// `size` times `ratio`, kept within what the settings file stores (a signed 16-bit number).
short scaledShort(short size, float ratio) {
    return static_cast<short>(std::clamp(std::lround(static_cast<float>(size) * ratio), 0L, 32000L));
}

} // namespace

void ImGuiUiScale::setup() {
    m_baseStyle = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO();
    // Both fonts are added now; which one is the default follows the scale (applyStyle()).
    m_bitmapFont = io.Fonts->AddFontDefaultBitmap();
    m_vectorFont = io.Fonts->AddFontDefaultVector();
    ImGuiSettingsHandler handler;
    handler.TypeName = "ConeyUi";
    handler.TypeHash = ImHashStr("ConeyUi");
    handler.ReadOpenFn = openSettings;
    handler.ReadLineFn = readSettingsLine;
    handler.ApplyAllFn = applySettings;
    handler.WriteAllFn = writeSettings;
    handler.UserData = this;
    ImGui::AddSettingsHandler(&handler);
}

void ImGuiUiScale::applyStyle(float scale) {
    // Made from the 1x style every time, so scales do not compound and 1x gives exactly the original back.
    ImGuiStyle& style = ImGui::GetStyle();
    style = m_baseStyle;
    style.ScaleAllSizes(scale);
    style.FontSizeBase = kBaseFontSize * scale;
    ImGui::GetIO().FontDefault = scale > 1.0F ? m_vectorFont : m_bitmapFont;
}

bool ImGuiUiScale::update(float displayHeight) {
    const float target = debug::uiScaleFor(m_display.uiScaleChoice, displayHeight);
    m_display.uiScaleApplied = target;
    if (target == m_applied) {
        return false;
    }
    const float previous = m_applied;
    m_applied = target;
    applyStyle(target);
    if (previous > 0.0F) {
        scaleOpenWindows(target / previous);
    }
    if (m_savedWindowsPending) {
        scaleSavedWindows();
    }
    return true;
}

void ImGuiUiScale::scaleOpenWindows(float ratio) {
    const ImGuiContext& context = *ImGui::GetCurrentContext();
    // Only the windows a person can resize: not popups, tooltips, children or windows that fit their contents.
    constexpr ImGuiWindowFlags kFixed = ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup |
                                        ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    for (ImGuiWindow* window : context.Windows) {
        if ((window->Flags & kFixed) != 0) {
            continue;
        }
        ImGui::SetWindowSize(window, ImVec2(window->SizeFull.x * ratio, window->SizeFull.y * ratio), ImGuiCond_Always);
    }
}

void ImGuiUiScale::scaleSavedWindows() {
    m_savedWindowsPending = false;
    const float ratio = m_applied / m_savedScale;
    if (ratio == 1.0F) {
        return;
    }
    ImGuiContext& context = *ImGui::GetCurrentContext();
    for (ImGuiWindowSettings* saved = context.SettingsWindows.begin(); saved != nullptr;
         saved = context.SettingsWindows.next_chunk(saved)) {
        saved->Size.x = scaledShort(saved->Size.x, ratio);
        saved->Size.y = scaledShort(saved->Size.y, ratio);
    }
    // From now on the sizes in the settings are at the applied scale.
    m_savedScale = m_applied;
}

void* ImGuiUiScale::openSettings(ImGuiContext* /*context*/, ImGuiSettingsHandler* handler, const char* name) {
    return std::string_view(name) == "Scale" ? handler->UserData : nullptr;
}

void ImGuiUiScale::readSettingsLine(ImGuiContext* /*context*/, ImGuiSettingsHandler* /*handler*/, void* entry,
                                    const char* line) {
    auto* self = static_cast<ImGuiUiScale*>(entry);
    const std::string_view text(line);
    constexpr std::string_view kApplied = "Applied=";
    constexpr std::string_view kChoice = "Choice=";
    if (text.starts_with(kApplied)) {
        const float saved = std::strtof(line + kApplied.size(), nullptr);
        if (saved >= debug::kUiScaleMin && saved <= debug::kUiScaleMax) {
            self->m_savedScale = saved;
        }
    } else if (text.starts_with(kChoice)) {
        const unsigned long choice = std::strtoul(line + kChoice.size(), nullptr, 10);
        if (choice < debug::uiScaleLabels().size()) {
            self->m_display.uiScaleChoice = static_cast<std::size_t>(choice);
        }
    }
}

void ImGuiUiScale::applySettings(ImGuiContext* /*context*/, ImGuiSettingsHandler* handler) {
    auto* self = static_cast<ImGuiUiScale*>(handler->UserData);
    self->m_savedWindowsPending = true;
    // Settings read after the first update() (a file loaded late) are scaled at once.
    if (self->m_applied > 0.0F) {
        self->scaleSavedWindows();
    }
}

void ImGuiUiScale::writeSettings(ImGuiContext* /*context*/, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
    const auto* self = static_cast<const ImGuiUiScale*>(handler->UserData);
    out->append("[ConeyUi][Scale]\n");
    out->appendf("Applied=%g\n", static_cast<double>(self->scale()));
    out->appendf("Choice=%u\n", static_cast<unsigned>(self->m_display.uiScaleChoice));
    out->append("\n");
}

} // namespace coney::platform
