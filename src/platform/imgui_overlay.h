// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include "core/error.h"
#include "debug/debug_session.h"
#include "platform/imgui_model_view.h"
#include "platform/render_engine.h"

namespace coney::platform {

/// The debug menus' developer overlay: the session's model drawn with Dear ImGui (ImGuiModelView) over the game,
/// driven by mouse and keyboard. F1 shows and hides it; it starts hidden, and hidden it draws nothing and takes no
/// input, so frames are exactly those of a run without it.
///
/// While a widget has the keyboard or the mouse is over a window, those events stay with the overlay: the window's
/// Escape does not quit, and the caller turns the keyboard pad off (wantsKeyboard()). Its drawing saves and restores
/// the OpenGL state librw relies on (Dear ImGui's OpenGL 3 backend does both), so librw's own state cache stays right.
///
/// Only with a window: a headless run has none (the NULL renderer has no OpenGL context). Guide:
/// docs/guides/debug-menu.md#the-developer-overlay.
class ImGuiOverlay {
  public:
    /// Starts Dear ImGui on `renderer`'s window and OpenGL context, over `session`'s model; both must outlive the
    /// overlay, which must be destroyed before the renderer stops. Fails with ErrorCode::PlatformFailure when the
    /// renderer has no window or a backend refuses. `iniPath` is where ImGui keeps window places (empty: nowhere).
    [[nodiscard]] static std::expected<std::unique_ptr<ImGuiOverlay>, Error>
    start(RenderEngine& renderer, debug::DebugSession& session, std::string iniPath);

    ~ImGuiOverlay();
    ImGuiOverlay(const ImGuiOverlay&) = delete;
    ImGuiOverlay& operator=(const ImGuiOverlay&) = delete;
    ImGuiOverlay(ImGuiOverlay&&) = delete;
    ImGuiOverlay& operator=(ImGuiOverlay&&) = delete;

    /// Shows one OS event (a `const SDL_Event*`) to the overlay; for Window::pumpEvents' filter. F1 toggles it. Returns
    /// true when the overlay took the event: F1, or a key or mouse event while it wants that device.
    bool handleEvent(const void* event);

    /// Draws the overlay over the frame, when shown: for the renderer's present overlay, inside the frame.
    void draw();

    /// Whether it is shown.
    [[nodiscard]] bool visible() const { return m_visible; }
    /// Shows or hides it.
    void setVisible(bool visible);
    /// Shows it for the next `frames` draws, then hides it: a test aid for checking that it leaves the frame as it
    /// found it (`--dev-overlay FRAMES`).
    void showForFrames(std::uint64_t frames);
    /// Whether it has the keyboard (a text box or a focused widget): the keyboard should then not reach the pads.
    [[nodiscard]] bool wantsKeyboard() const;
    /// Whether the mouse is over it.
    [[nodiscard]] bool wantsMouse() const;

  private:
    ImGuiOverlay(debug::DebugSession& session, std::string iniPath);

    ImGuiModelView m_view;
    std::string m_iniPath; // kept alive for ImGui, which holds only the pointer
    bool m_visible = false;
    bool m_sdlStarted = false;     // the SDL3 backend is up and must be shut down
    bool m_glStarted = false;      // the OpenGL 3 backend is up and must be shut down
    std::uint64_t m_hideAfter = 0; // draws left before showForFrames() hides it; 0: no limit
};

} // namespace coney::platform
