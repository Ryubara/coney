// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

#include "console/console.h"
#include "console/console_pad.h"
#include "core/error.h"
#include "platform/console_view.h"
#include "platform/render_engine.h"
#include "platform/ui_scale.h"

struct ImGuiContext;

// @coney-only the Coney console's F8 bar (docs/guides/console.md#front-end); the original has no console.

namespace coney::platform {

/// How the bar runs.
struct ConsoleUiSettings {
    /// A test-mode run: F8 is not caught and no settings file is read or written
    /// (docs/guides/studio/console.md#test-mode).
    bool testMode = false;
    /// Where the bar keeps the pinned terminal and its windows' places (`coney-console.ini`); empty: nowhere.
    std::string iniPath;
};

/// The player console's front end in a window (docs/guides/console.md#front-end): the F8 bar (ConsoleView) drawn with
/// Dear ImGui over the game, in its own Dear ImGui context, so it shares nothing with the debug tools' overlay in a
/// build that has both. F8 opens and closes it, Escape closes it; while it is open the keyboard is the console's and
/// the pad is held by the console's gate (console::ConsolePad), which also opens it with SELECT and R3. Closed with
/// nothing pinned it draws nothing and takes only F8, so frames are those of a run without it.
///
/// Its drawing saves and restores the OpenGL state librw relies on (Dear ImGui's OpenGL 3 backend does both). Only with
/// a window and an OpenGL context: a headless run, or the GPU renderer, runs without the bar.
class ConsoleUi {
  public:
    /// Starts the bar on `renderer`'s window and OpenGL context over `console`, opened and closed through `pad`; all
    /// three must outlive it, and it must be destroyed before the renderer stops. Fails with
    /// ErrorCode::PlatformFailure when the renderer has no window with an OpenGL context or a backend refuses.
    [[nodiscard]] static std::expected<std::unique_ptr<ConsoleUi>, Error>
    start(RenderEngine& renderer, console::Console& console, console::ConsolePad& pad, ConsoleUiSettings settings);

    ~ConsoleUi();
    ConsoleUi(const ConsoleUi&) = delete;
    ConsoleUi& operator=(const ConsoleUi&) = delete;
    ConsoleUi(ConsoleUi&&) = delete;
    ConsoleUi& operator=(ConsoleUi&&) = delete;

    /// Shows one OS event (a `const SDL_Event*`) to the bar; for Window::pumpEvents' filter. Returns true when the bar
    /// took it: F8 (outside test mode), Escape while open, any keyboard event while open, and the mouse over its
    /// windows.
    bool handleEvent(const void* event);

    /// Draws the bar over the frame when it is open or the terminal is pinned: for the renderer's present overlay.
    void draw();

    /// Whether the console is open.
    [[nodiscard]] bool open() const { return m_pad.open(); }
    /// Opens it, the line taking the keyboard, or closes it.
    void setOpen(bool open);
    /// Opens it for the next `frames` draws, then closes it: the test aid that checks the frame is untouched once it
    /// closes (`--console-open FRAMES`).
    void showForFrames(std::uint64_t frames);
    /// Whether the keyboard should stay away from the pads: always while the console is open.
    [[nodiscard]] bool wantsKeyboard() const { return m_pad.open(); }

  private:
    // Makes the bar's context current for its lifetime and puts the one before back after.
    class ContextScope;

    ConsoleUi(console::Console& console, console::ConsolePad& pad, ConsoleUiSettings settings);

    console::ConsolePad& m_pad;
    ConsoleUiSettings m_settings;
    std::size_t m_scaleChoice = 0; // Auto: the bar has no override of its own
    float m_scaleApplied = 1.0F;
    ConsoleView m_view;
    ImGuiUiScale m_scale;
    ImGuiContext* m_context = nullptr;
    bool m_sdlStarted = false;     // the SDL3 backend is up and must be shut down
    bool m_glStarted = false;      // the OpenGL 3 backend is up and must be shut down
    std::uint64_t m_hideAfter = 0; // draws left before showForFrames() closes it; 0: no limit
};

/// Where the bar keeps its settings outside test mode: `coney-console.ini` in SDL's preference folder for Coney, beside
/// `coney.log` and `console-history.txt` (docs/guides/run-log.md#outputs); nothing when SDL cannot give that folder.
[[nodiscard]] std::optional<std::string> consoleSettingsPath();

} // namespace coney::platform
