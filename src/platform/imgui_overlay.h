// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include "core/error.h"
#include "debug/debug_session.h"
#include "platform/imgui_model_view.h"
#include "platform/imgui_ui_scale.h"
#include "platform/render_engine.h"

struct ImGuiContext;
struct ImGuiIO;
struct SDL_GPUCommandBuffer;
struct SDL_GPUDevice;
struct SDL_GPUTexture;

namespace coney::platform {

/// The debug menus' developer overlay: the session's model drawn with Dear ImGui (ImGuiModelView) over the game,
/// driven by mouse and keyboard. F1 shows and hides it; it starts hidden, and hidden it draws nothing and takes no
/// input, so frames are exactly those of a run without it.
///
/// While a widget has the keyboard or the mouse is over a window, those events stay with the overlay: the window's
/// Escape does not quit, and the caller turns the keyboard pad off (wantsKeyboard()). On GL3 its drawing saves and
/// restores the OpenGL state librw relies on (Dear ImGui's OpenGL 3 backend does both), so librw's own state cache
/// stays right; on the GPU renderer Dear ImGui's SDL GPU backend draws it into the renderer's frame
/// (docs/engine/gpu-renderer.md#overlay).
///
/// Only with a window: a headless run has none (the NULL renderer has no window and no OpenGL context). Guide:
/// docs/guides/debug-menu.md#the-developer-overlay.
class ImGuiOverlay {
  public:
    /// Sets the Dear ImGui input and window options the overlay runs with: keyboard navigation, and docking (a page
    /// window can be dragged by its title onto another to tab with it or split beside it, and the layout is kept in
    /// the settings file). Apart from `start`, so the tests set a bare context up the same way.
    static void configureIo(ImGuiIO& io);

    /// Starts Dear ImGui on `renderer`'s window, over `session`'s model, with the backend its renderer takes (OpenGL 3
    /// on its context for GL3, SDL GPU on its device for the GPU renderer); both must outlive the overlay, which must
    /// be destroyed before the renderer stops. Fails with ErrorCode::PlatformFailure when the renderer has no window
    /// (or, for the GPU renderer, no 2D drawing to draw over) or a backend refuses. `iniPath` is where ImGui keeps
    /// window places (empty: nowhere).
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

    /// Draws the overlay over the frame, when shown: for the renderer's present overlay, inside the frame. On the GPU
    /// renderer it only builds Dear ImGui's draw data; the renderer then calls back to record them into its frame
    /// (RenderEngine::addGpuOverlay()), still before the frame is presented.
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

    /// Records the draw data draw() built into `commands`, in a render pass over `target` that keeps what is there:
    /// the GPU renderer's callback.
    void recordGpu(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target);

    ImGuiModelView m_view;
    ImGuiUiScale m_scale;
    std::string m_iniPath; // kept alive for ImGui, which holds only the pointer
    bool m_visible = false;
    bool m_sdlStarted = false;            // the SDL3 backend is up and must be shut down
    bool m_glStarted = false;             // the OpenGL 3 backend is up and must be shut down
    bool m_gpuStarted = false;            // the SDL GPU backend is up and must be shut down
    bool m_gpuPending = false;            // draw() built draw data the GPU renderer has not recorded yet
    RenderEngine* m_renderer = nullptr;   // the GPU renderer's engine, whose drawing is removed at destruction
    GpuOverlayHandle m_gpuHandle = 0;     // the drawing added to m_renderer
    ImGuiContext* m_context = nullptr;    // this overlay's Dear ImGui context, made current before it draws
    SDL_GPUDevice* m_gpuDevice = nullptr; // the GPU backend's device, waited on before the backend stops
    std::uint64_t m_hideAfter = 0;        // draws left before showForFrames() hides it; 0: no limit
};

} // namespace coney::platform
