// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/console_view.h"

#include <imgui.h>

namespace coney::platform {

void ConsoleView::addSettingsHandler() {}

void ConsoleView::setPinned(bool pinned) { m_pinned = pinned; }

void ConsoleView::draw() {}

void ConsoleView::drawBar() {}

void ConsoleView::drawTerminal(bool /*interactive*/) {}

void ConsoleView::drawLine() {}

void ConsoleView::drawPicker() {}

int ConsoleView::lineCallback(ImGuiInputTextCallbackData* /*data*/) { return 0; }

void* ConsoleView::openSettings(ImGuiContext* /*context*/, ImGuiSettingsHandler* /*handler*/, const char* /*name*/) {
    return nullptr;
}

void ConsoleView::readSettingsLine(ImGuiContext* /*context*/, ImGuiSettingsHandler* /*handler*/, void* /*entry*/,
                                   const char* /*line*/) {}

void ConsoleView::writeSettings(ImGuiContext* /*context*/, ImGuiSettingsHandler* /*handler*/,
                                ImGuiTextBuffer* /*out*/) {}

} // namespace coney::platform
