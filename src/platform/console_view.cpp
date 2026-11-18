// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/console_view.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <format>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h> // the settings handler
#include <imgui_stdlib.h>

#include "console/command_picker.h"
#include "console/terminal.h"

namespace coney::platform {

namespace {

// The output's colours by kind: the echoed line dimmed, an error red, a result the text's own.
constexpr ImVec4 kEchoColour{0.62F, 0.66F, 0.72F, 1.0F};
constexpr ImVec4 kErrorColour{1.0F, 0.45F, 0.42F, 1.0F};
// The terminal's share of the screen's height when it first opens.
constexpr float kTerminalHeightShare = 0.45F;
// The picker's share of the screen's width, on the right.
constexpr float kPickerWidthShare = 0.32F;
// The settings file's section and entry for the panels.
constexpr const char* kSettingsType = "ConeyConsole";
constexpr const char* kSettingsEntry = "Panels";
constexpr std::string_view kPinnedKey = "Pinned=";

// The bar's reminder of how to close the console.
constexpr const char* kCloseHint = "F8 or SELECT + R3 closes";

} // namespace

void ConsoleView::addSettingsHandler() {
    ImGuiSettingsHandler handler;
    handler.TypeName = kSettingsType;
    handler.TypeHash = ImHashStr(kSettingsType);
    handler.ReadOpenFn = openSettings;
    handler.ReadLineFn = readSettingsLine;
    handler.WriteAllFn = writeSettings;
    handler.UserData = this;
    ImGui::AddSettingsHandler(&handler);
}

void ConsoleView::setPinned(bool pinned) {
    if (pinned != m_pinned) {
        m_pinned = pinned;
        ImGui::MarkIniSettingsDirty();
    }
}

void ConsoleView::draw() {
    if (!m_pad.open()) {
        // Closed: only a pinned terminal, which takes no input; nothing at all otherwise, so the frame is untouched.
        if (m_pinned) {
            drawTerminal(false);
        }
        return;
    }
    drawBar();
    drawTerminal(true);
    if (m_pad.openedBy() == console::OpenedBy::Pad) {
        drawPicker();
    }
}

void ConsoleView::drawBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    ImGui::TextUnformatted(kTitle);
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Pin the terminal", nullptr, m_pinned)) {
            setPinned(!m_pinned);
        }
        if (ImGui::MenuItem("Command picker", nullptr, m_pad.openedBy() == console::OpenedBy::Pad)) {
            const bool picker = m_pad.openedBy() == console::OpenedBy::Pad;
            m_pad.setOpen(false);
            m_pad.setOpen(true, picker ? console::OpenedBy::Keyboard : console::OpenedBy::Pad);
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Close")) {
        m_pad.setOpen(false);
    }
    // The hint at the right end of the bar.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float hintWidth = ImGui::CalcTextSize(kCloseHint).x + style.ItemSpacing.x + style.WindowPadding.x * 2.0F;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - hintWidth));
    ImGui::TextDisabled("%s", kCloseHint);
    ImGui::EndMainMenuBar();
}

void ConsoleView::drawTerminal(bool interactive) {
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    const float top = ImGui::GetFrameHeight(); // below the bar
    ImGui::SetNextWindowPos(ImVec2(viewport.WorkPos.x, viewport.Pos.y + top), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(viewport.Size.x, viewport.Size.y * kTerminalHeightShare), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!interactive) {
        // Pinned with the console closed: on screen, never in the way of the game's input.
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::SetNextWindowBgAlpha(0.5F);
    }
    if (!ImGui::Begin(kTerminalWindow, nullptr, flags)) {
        ImGui::End();
        return;
    }
    // The output fills what the line and its candidates leave.
    const float lineHeight = ImGui::GetFrameHeightWithSpacing();
    const std::size_t shown = std::min(m_editor.candidates().size(), kCandidatesShown);
    const float footer =
        interactive ? lineHeight + static_cast<float>(shown) * ImGui::GetTextLineHeightWithSpacing() : 0.0F;
    if (ImGui::BeginChild("output", ImVec2(0.0F, -footer), ImGuiChildFlags_None,
                          interactive ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_NoInputs)) {
        const std::deque<console::OutputLine>& output = m_console.terminal().output();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(output.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const console::OutputLine& line = output[static_cast<std::size_t>(i)];
                const std::string text =
                    line.source.empty() ? line.text : std::format("[{}] {}", line.source, line.text);
                if (line.kind == console::LineKind::Result) {
                    ImGui::TextUnformatted(text.c_str());
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          line.kind == console::LineKind::Error ? kErrorColour : kEchoColour);
                    ImGui::TextUnformatted(text.c_str());
                    ImGui::PopStyleColor();
                }
            }
        }
        // New lines: follow them to the bottom.
        if (output.size() != m_outputSeen) {
            m_outputSeen = output.size();
            ImGui::SetScrollHereY(1.0F);
        }
    }
    ImGui::EndChild();
    if (interactive) {
        drawLine();
    }
    ImGui::End();
}

void ConsoleView::drawLine() {
    // The candidates while typing, above the line.
    const std::vector<std::string>& candidates = m_editor.candidates();
    for (std::size_t i = 0; i < std::min(candidates.size(), kCandidatesShown); ++i) {
        const bool last = i + 1 == kCandidatesShown && candidates.size() > kCandidatesShown;
        ImGui::TextDisabled("%s%s", candidates[i].c_str(), last ? "  ..." : "");
    }
    if (m_focusLine) {
        ImGui::SetKeyboardFocusHere();
        m_focusLine = false;
    }
    ImGui::SetNextItemWidth(-1.0F);
    constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                           ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("##line", &m_buffer, kFlags, &ConsoleView::lineCallback, this)) {
        m_editor.setText(m_buffer);
        (void)m_editor.submit();
        m_buffer.clear();
        // Enter leaves the text box; the line keeps the keyboard for the next command.
        ImGui::SetKeyboardFocusHere(-1);
    } else if (m_buffer != m_editor.text()) {
        m_editor.setText(m_buffer);
    }
}

int ConsoleView::lineCallback(ImGuiInputTextCallbackData* data) {
    auto& self = *static_cast<ConsoleView*>(data->UserData);
    self.m_editor.setText(std::string_view(data->Buf, static_cast<std::size_t>(data->BufTextLen)));
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        if (!self.m_editor.complete()) {
            return 0;
        }
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        if (data->EventKey == ImGuiKey_UpArrow) {
            self.m_editor.previous();
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            self.m_editor.next();
        }
    }
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, self.m_editor.text().c_str());
    return 0;
}

void ConsoleView::drawPicker() {
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    const float width = viewport.Size.x * kPickerWidthShare;
    const float top = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(
        ImVec2(viewport.Pos.x + viewport.Size.x - width, viewport.Pos.y + top + viewport.Size.y * kTerminalHeightShare),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(width, viewport.Size.y * (1.0F - kTerminalHeightShare) - top),
                             ImGuiCond_FirstUseEver);
    // The pad drives it through the console's gate, so it never takes Dear ImGui's focus or navigation.
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!ImGui::Begin(kPickerWindow, nullptr, kFlags)) {
        ImGui::End();
        return;
    }
    const console::CommandPicker& picker = m_pad.picker();
    const std::vector<console::PickerRow> rows = picker.rows();
    const bool commands = picker.page() == console::PickerPage::Commands;
    if (commands) {
        ImGui::TextDisabled("d-pad chooses, cross runs, circle closes");
    } else {
        ImGui::TextDisabled("left and right change, cross goes on, circle goes back");
        ImGui::Text("> %s", picker.line().c_str());
    }
    std::string_view group;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const console::PickerRow& row = rows[i];
        if (commands && row.value != group) {
            group = row.value;
            ImGui::SeparatorText(row.value.c_str());
        }
        const std::string label =
            commands || row.value.empty() ? row.label : std::format("{}: {}", row.label, row.value);
        const bool selected = i == picker.selected();
        ImGui::Selectable(label.c_str(), selected);
        if (selected) {
            ImGui::SetScrollHereY();
        }
    }
    if (picker.selected() < rows.size() && !rows[picker.selected()].help.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", rows[picker.selected()].help.c_str());
    }
    ImGui::End();
}

// The settings file's `[ConeyConsole][Panels]` entry is this view.
void* ConsoleView::openSettings(ImGuiContext* /*context*/, ImGuiSettingsHandler* handler, const char* name) {
    return std::string_view(name) == kSettingsEntry ? handler->UserData : nullptr;
}

// Reads one line of that entry: `Pinned=`.
void ConsoleView::readSettingsLine(ImGuiContext* /*context*/, ImGuiSettingsHandler* /*handler*/, void* entry,
                                   const char* line) {
    auto& self = *static_cast<ConsoleView*>(entry);
    const std::string_view text(line);
    if (text.starts_with(kPinnedKey)) {
        self.m_pinned = std::strtol(line + kPinnedKey.size(), nullptr, 10) != 0;
    }
}

// Writes the entry with the pinned terminal.
void ConsoleView::writeSettings(ImGuiContext* /*context*/, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
    const auto& self = *static_cast<const ConsoleView*>(handler->UserData);
    out->appendf("[%s][%s]\n", kSettingsType, kSettingsEntry);
    out->appendf("Pinned=%d\n", self.m_pinned ? 1 : 0);
    out->append("\n");
}

} // namespace coney::platform
