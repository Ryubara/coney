// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/studio_host.h"

#include <algorithm>
#include <format>
#include <utility>

#include <SDL3/SDL.h>

namespace coney::platform {

namespace {

// SDL's dialog callback: keeps the first folder chosen (or none) for deliver(). `userdata` is a heap copy of the
// pending answer's shared pointer, freed here.
void SDLCALL folderChosen(void* userdata, const char* const* files, int /*filter*/) {
    auto* holder = static_cast<std::shared_ptr<SdlStudioHost::Pending>*>(userdata);
    {
        const std::scoped_lock guard((*holder)->lock);
        (*holder)->answered = true;
        if (files != nullptr && files[0] != nullptr) {
            (*holder)->folder = std::filesystem::path(reinterpret_cast<const char8_t*>(files[0]));
        }
    }
    delete holder;
}

} // namespace

SdlStudioHost::~SdlStudioHost() {
    // The cursor is given back if the program ends with the right button held.
    static_cast<void>(setRelativeMouse(false));
    // Releasing a process handle leaves the process running: a Coney started from Studio outlives it.
    for (void* process : m_processes) {
        SDL_DestroyProcess(static_cast<SDL_Process*>(process));
    }
}

void SdlStudioHost::pickFolder(std::string_view title, std::function<void(std::optional<std::filesystem::path>)> done) {
    static_cast<void>(title); // SDL 3's simple folder dialog takes no title
    m_pending = std::make_shared<Pending>();
    m_done = std::move(done);
    SDL_ShowOpenFolderDialog(folderChosen, new std::shared_ptr<Pending>(m_pending), static_cast<SDL_Window*>(m_window),
                             nullptr, false);
}

std::expected<void, Error> SdlStudioHost::startProcess(const std::vector<std::string>& command) {
    std::vector<const char*> args;
    args.reserve(command.size() + 1);
    for (const std::string& arg : command) {
        args.push_back(arg.c_str());
    }
    args.push_back(nullptr);
    SDL_Process* process = SDL_CreateProcess(args.data(), false);
    if (process == nullptr) {
        return fail(ErrorCode::PlatformFailure, std::format("could not start {}: {}", command.front(), SDL_GetError()));
    }
    m_processes.push_back(process);
    return {};
}

std::uint64_t SdlStudioHost::drawLevel(const studio::LevelDocument& level, const studio::LevelView& view) {
    return m_levels.draw(level, view);
}

bool SdlStudioHost::setRelativeMouse(bool on) {
    auto* window = static_cast<SDL_Window*>(m_window);
    if (on == m_relative) {
        return m_relative;
    }
    if (on) {
        // The cursor goes where it was when it comes back: relative mode hides it and keeps it inside the window, and
        // the movement comes as SDL_EVENT_MOUSE_MOTION's xrel and yrel (onEvent()).
        SDL_GetMouseState(&m_restoreX, &m_restoreY);
        if (!SDL_SetWindowRelativeMouseMode(window, true)) {
            return false;
        }
        m_relative = true;
        m_motion = {};
        return true;
    }
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_WarpMouseInWindow(window, m_restoreX, m_restoreY);
    m_relative = false;
    m_motion = {};
    return false;
}

studio::MouseMotion SdlStudioHost::takeRelativeMotion() { return std::exchange(m_motion, {}); }

void SdlStudioHost::onEvent(const void* sdlEvent) {
    const auto* event = static_cast<const SDL_Event*>(sdlEvent);
    if (!m_relative || event->type != SDL_EVENT_MOUSE_MOTION) {
        return;
    }
    // SDL reports the movement in window units; the display's pixel density turns them into physical pixels (1 on
    // Windows, 2 on a Retina Mac), which Studio then counts in the display's scale-1 pixels.
    const float density = std::max(1.0F, SDL_GetWindowPixelDensity(static_cast<SDL_Window*>(m_window)));
    m_motion.x += event->motion.xrel * density;
    m_motion.y += event->motion.yrel * density;
}

void SdlStudioHost::deliver() {
    if (!m_pending) {
        return;
    }
    std::optional<std::filesystem::path> folder;
    {
        const std::scoped_lock guard(m_pending->lock);
        if (!m_pending->answered) {
            return;
        }
        folder = m_pending->folder;
    }
    m_pending.reset();
    auto done = std::exchange(m_done, {});
    if (done) {
        done(std::move(folder));
    }
}

std::string SdlStudioHost::takeDrawError() {
    const std::string& error = m_levels.lastError();
    if (error == m_reported) {
        return {};
    }
    m_reported = error;
    return error;
}

} // namespace coney::platform
