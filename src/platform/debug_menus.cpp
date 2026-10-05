// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/debug_menus.h"

#include <utility>

#include <SDL3/SDL.h>

namespace coney::platform {

std::string defaultTunablesPath() {
    char* folder = SDL_GetPrefPath("Coney", "Coney");
    if (folder == nullptr) {
        return {};
    }
    std::string path = std::string(folder) + "coney-tunables.ini";
    SDL_free(folder);
    return path;
}

PadMenuOverlay::PadMenuOverlay(debug::DebugSession& session, std::optional<graphics::Font> gameFont)
    : m_session(session), m_gameFont(gameFont.has_value()) {
    if (gameFont) {
        m_painter = std::make_unique<gui::GameFontPainter>(std::move(*gameFont), kGameFontLine);
    } else {
        m_painter = std::make_unique<gui::BitmapTextPainter>();
    }
}

} // namespace coney::platform
