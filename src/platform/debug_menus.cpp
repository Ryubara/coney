// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/debug_menus.h"

#include <string_view>
#include <utility>

#include <SDL3/SDL.h>

namespace coney::platform {

namespace {

// `name` in SDL's preference folder for Coney; empty when SDL cannot give the folder.
std::string prefPath(std::string_view name) {
    char* folder = SDL_GetPrefPath("Coney", "Coney");
    if (folder == nullptr) {
        return {};
    }
    std::string path = std::string(folder) + std::string(name);
    SDL_free(folder);
    return path;
}

} // namespace

std::string defaultTunablesPath() { return prefPath("coney-tunables.ini"); }

std::string defaultOverlayLayoutPath() { return prefPath("coney-imgui.ini"); }

PadMenuOverlay::PadMenuOverlay(debug::DebugSession& session, std::optional<graphics::Font> gameFont)
    : m_session(session), m_gameFont(gameFont.has_value()) {
    if (gameFont) {
        m_painter = std::make_unique<gui::GameFontPainter>(std::move(*gameFont), kGameFontLine);
    } else {
        m_painter = std::make_unique<gui::BitmapTextPainter>();
    }
}

} // namespace coney::platform
