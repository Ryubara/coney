// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/imgui_overlay.h"

#include <utility>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

namespace coney::platform {

namespace {

// The menu bar's reminder of how to hide the overlay.
constexpr std::string_view kHideLabel = "F1 hides";

// Whether `event` comes from the keyboard.
bool isKeyboardEvent(const SDL_Event& event) {
    return event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT ||
           event.type == SDL_EVENT_TEXT_EDITING;
}

// Whether `event` comes from the mouse.
bool isMouseEvent(const SDL_Event& event) {
    return event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
           event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL;
}

} // namespace

void ImGuiOverlay::configureIo(ImGuiIO& io) {
    // Mouse and keyboard only: the gamepads belong to the game and the pad menu.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Windows dock into each other (tabs and splits) inside the viewport. Multi-viewport stays off: it is the part of
    // the docking release that varies most by platform (docs/guides/studio/technology.md#gui-choice).
}

ImGuiOverlay::ImGuiOverlay(debug::DebugSession& session, std::string iniPath)
    : m_view(session.model()), m_scale(session.display()), m_iniPath(std::move(iniPath)) {}

std::expected<std::unique_ptr<ImGuiOverlay>, Error>
ImGuiOverlay::start(RenderEngine& renderer, debug::DebugSession& session, std::string iniPath) {
    const auto [window, context] = renderer.sdlWindowAndContext();
    if (window == nullptr || context == nullptr) {
        return fail(ErrorCode::PlatformFailure, "the developer overlay needs a window with an OpenGL context");
    }
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<ImGuiOverlay> overlay(new ImGuiOverlay(session, std::move(iniPath)));
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = overlay->m_iniPath.empty() ? nullptr : overlay->m_iniPath.c_str();
    configureIo(io);
    ImGui::StyleColorsDark();
    overlay->m_view.keepOpenPagesInSettings();
    overlay->m_scale.setup();
    // From here on the destructor undoes whatever started, so an early return cleans up.
    overlay->m_sdlStarted = ImGui_ImplSDL3_InitForOpenGL(static_cast<SDL_Window*>(window), context);
    if (!overlay->m_sdlStarted) {
        return fail(ErrorCode::PlatformFailure, "Dear ImGui's SDL3 backend did not start");
    }
    // Its default GLSL version (130, 150 on macOS) suits librw's OpenGL 3.3 core context.
    overlay->m_glStarted = ImGui_ImplOpenGL3_Init(nullptr);
    if (!overlay->m_glStarted) {
        return fail(ErrorCode::PlatformFailure, "Dear ImGui's OpenGL 3 backend did not start");
    }
    // The settings are read now, not at the first frame, so the saved scale choice is known before the first frame is
    // sized (Dear ImGui does not read them again).
    if (io.IniFilename != nullptr) {
        ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    }
    return overlay;
}

// Undoes whatever start() got as far as, in reverse order, so an early failure leaves nothing running.
ImGuiOverlay::~ImGuiOverlay() {
    if (m_glStarted) {
        ImGui_ImplOpenGL3_Shutdown();
    }
    if (m_sdlStarted) {
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::DestroyContext();
}

bool ImGuiOverlay::handleEvent(const void* event) {
    const auto& sdlEvent = *static_cast<const SDL_Event*>(event);
    if (sdlEvent.type == SDL_EVENT_KEY_DOWN && sdlEvent.key.key == SDLK_F1 && !sdlEvent.key.repeat) {
        setVisible(!m_visible);
        return true;
    }
    // ImGui sees every event, shown or not, so its idea of held keys and the mouse stays right.
    ImGui_ImplSDL3_ProcessEvent(&sdlEvent);
    return (isKeyboardEvent(sdlEvent) && wantsKeyboard()) || (isMouseEvent(sdlEvent) && wantsMouse());
}

void ImGuiOverlay::setVisible(bool visible) {
    m_visible = visible;
    m_hideAfter = 0;
}

void ImGuiOverlay::showForFrames(std::uint64_t frames) {
    setVisible(frames > 0);
    m_hideAfter = frames;
}

bool ImGuiOverlay::wantsKeyboard() const { return m_visible && ImGui::GetIO().WantCaptureKeyboard; }

bool ImGuiOverlay::wantsMouse() const { return m_visible && ImGui::GetIO().WantCaptureMouse; }

void ImGuiOverlay::draw() {
    if (!m_visible) {
        return;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    // The display's size is known now (the backend just read the window): size the widgets and the font for it, so a
    // resized window or a new display is followed from the next frame on.
    m_scale.update(ImGui::GetIO().DisplaySize.y);
    m_view.setUiScale(m_scale.scale());
    ImGui::NewFrame();
    m_view.draw(kHideLabel);
    ImGui::Render();
    // The OpenGL 3 backend saves the GL state it changes and restores it afterwards.
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (m_hideAfter > 0) {
        --m_hideAfter;
        m_visible = m_hideAfter > 0;
    }
}

} // namespace coney::platform
