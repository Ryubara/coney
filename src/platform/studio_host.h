// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "platform/studio_level_renderer.h"
#include "studio/studio_app.h"

namespace coney::platform {

/// What Coney Studio asks of the operating system, through SDL3: the folder picker (SDL's dialog), child processes
/// (SDL's processes, for Run in Coney) and the Level view's drawing (StudioLevelRenderer). The picker's answer may
/// come on another thread, so it is kept until the main loop hands it over (deliver()). Coney's own; the original
/// has no editor.
class SdlStudioHost final : public studio::StudioHost {
  public:
    /// A host for the window `sdlWindow` (an SDL_Window*, opaque here; the picker's parent) drawing with `engine`.
    SdlStudioHost(void* sdlWindow, RenderEngine& engine) : m_window(sdlWindow), m_levels(engine) {}
    /// Releases the handles of the processes it started, which go on running.
    ~SdlStudioHost() override;
    SdlStudioHost(const SdlStudioHost&) = delete;
    SdlStudioHost& operator=(const SdlStudioHost&) = delete;
    SdlStudioHost(SdlStudioHost&&) = delete;
    SdlStudioHost& operator=(SdlStudioHost&&) = delete;

    void pickFolder(std::string_view title, std::function<void(std::optional<std::filesystem::path>)> done) override;
    std::expected<void, Error> startProcess(const std::vector<std::string>& command) override;
    std::uint64_t drawLevel(const studio::LevelDocument& level, const studio::LevelView& view) override;
    bool setRelativeMouse(bool on) override;
    studio::MouseMotion takeRelativeMotion() override;

    /// Feeds one SDL event (an SDL_Event*, opaque here) to the host: mouse motion is added up while relative mode is
    /// on.
    void onEvent(const void* sdlEvent);

    /// Calls the picker's `done` with its answer, once it has come; on the main loop's thread, each frame.
    void deliver();
    /// Why the Level view could not be drawn, once per new reason (empty otherwise): for the Console.
    [[nodiscard]] std::string takeDrawError();

    // The picker's answer, written by SDL's callback (any thread) and read by deliver().
    struct Pending {
        std::mutex lock;
        bool answered = false;
        std::optional<std::filesystem::path> folder;
    };

  private:
    void* m_window;
    StudioLevelRenderer m_levels;
    std::shared_ptr<Pending> m_pending;
    std::function<void(std::optional<std::filesystem::path>)> m_done;
    std::vector<void*> m_processes; // SDL_Process*, released (not stopped) at the end
    std::string m_reported;         // the last draw error handed to the Console
    bool m_relative = false;        // relative mouse mode is on
    float m_restoreX = 0.0F;        // where the cursor was when relative mode began, window coordinates
    float m_restoreY = 0.0F;
    studio::MouseMotion m_motion; // movement since takeRelativeMotion(), physical pixels
};

} // namespace coney::platform
