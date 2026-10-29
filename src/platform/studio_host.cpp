// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/studio_host.h"

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

// T stub: relative mouse mode is not built yet.
bool SdlStudioHost::setRelativeMouse(bool /*on*/) { return false; }

studio::MouseMotion SdlStudioHost::takeRelativeMotion() { return {}; }

void SdlStudioHost::onEvent(const void* /*sdlEvent*/) {}

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
