// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/imgui_overlay.h"

#include <format>
#include <optional>
#include <string_view>
#include <utility>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
}

ImGuiOverlay::ImGuiOverlay(debug::DebugSession& session, std::string iniPath)
    : m_view(session.model()), m_scale(session.display().uiScaleChoice, session.display().uiScaleApplied),
      m_iniPath(std::move(iniPath)) {}

std::expected<std::unique_ptr<ImGuiOverlay>, Error>
ImGuiOverlay::start(RenderEngine& renderer, debug::DebugSession& session, std::string iniPath) {
    const auto [window, context] = renderer.sdlWindowAndContext();
    const bool gpu = renderer.backend() == RenderBackend::Gpu;
    if (window == nullptr || (!gpu && context == nullptr)) {
        return fail(ErrorCode::PlatformFailure, "the developer overlay needs a window with an OpenGL context");
    }
    // The GPU renderer's overlay draws into its frame, so it needs the 2D drawing that makes one.
    const std::optional<GpuOverlayTarget> target = gpu ? renderer.gpuOverlayTarget() : std::nullopt;
    if (gpu && !target) {
        return fail(ErrorCode::PlatformFailure,
                    std::format("the developer overlay needs the GPU renderer's 2D drawing ({})",
                                renderer.gpuDrawingProblem()));
    }
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<ImGuiOverlay> overlay(new ImGuiOverlay(session, std::move(iniPath)));
    overlay->m_context = ImGui::CreateContext();
    // With another context already current, CreateContext() hands that one back as current: set up this one.
    ImGui::SetCurrentContext(overlay->m_context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = overlay->m_iniPath.empty() ? nullptr : overlay->m_iniPath.c_str();
    configureIo(io);
    ImGui::StyleColorsDark();
    overlay->m_view.keepOpenPagesInSettings();
    overlay->m_scale.setup();
    // From here on the destructor undoes whatever started, so an early return cleans up.
    auto* sdlWindow = static_cast<SDL_Window*>(window);
    overlay->m_sdlStarted =
        gpu ? ImGui_ImplSDL3_InitForSDLGPU(sdlWindow) : ImGui_ImplSDL3_InitForOpenGL(sdlWindow, context);
    if (!overlay->m_sdlStarted) {
        return fail(ErrorCode::PlatformFailure, "Dear ImGui's SDL3 backend did not start");
    }
    if (gpu) {
        // Drawn into the renderer's own target, whose format and device it names (not the swapchain's).
        ImGui_ImplSDLGPU3_InitInfo info;
        info.Device = target->device;
        info.ColorTargetFormat = static_cast<SDL_GPUTextureFormat>(target->colourFormat);
        info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        overlay->m_gpuStarted = ImGui_ImplSDLGPU3_Init(&info);
        if (!overlay->m_gpuStarted) {
            return fail(ErrorCode::PlatformFailure, "Dear ImGui's SDL GPU backend did not start");
        }
        overlay->m_gpuDevice = target->device;
        // The renderer calls back inside present(), after the frame's 2D passes.
        overlay->m_renderer = &renderer;
        ImGuiOverlay* raw = overlay.get();
        overlay->m_gpuHandle = renderer.addGpuOverlay([raw](SDL_GPUCommandBuffer* commands, SDL_GPUTexture* drawn,
                                                            graphics::Extent) { raw->recordGpu(commands, drawn); });
    } else {
        // Its default GLSL version (130, 150 on macOS) suits librw's OpenGL 3.3 core context.
        overlay->m_glStarted = ImGui_ImplOpenGL3_Init(nullptr);
        if (!overlay->m_glStarted) {
            return fail(ErrorCode::PlatformFailure, "Dear ImGui's OpenGL 3 backend did not start");
        }
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
    // Whichever overlay's context was current last, the backends shut down and the context destroyed are this one's.
    ImGui::SetCurrentContext(m_context);
    if (m_renderer != nullptr) {
        m_renderer->removeGpuOverlay(m_gpuHandle);
    }
    if (m_gpuStarted) {
        // The GPU may still be drawing the last frame's overlay from the backend's buffers.
        SDL_WaitForGPUIdle(m_gpuDevice);
        ImGui_ImplSDLGPU3_Shutdown();
    }
    if (m_glStarted) {
        ImGui_ImplOpenGL3_Shutdown();
    }
    if (m_sdlStarted) {
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::DestroyContext(m_context);
}

bool ImGuiOverlay::handleEvent(const void* event) {
    const auto& sdlEvent = *static_cast<const SDL_Event*>(event);
    if (sdlEvent.type == SDL_EVENT_KEY_DOWN && sdlEvent.key.key == SDLK_F1 && !sdlEvent.key.repeat) {
        setVisible(!m_visible);
        return true;
    }
    // ImGui sees every event, shown or not, so its idea of held keys and the mouse stays right.
    ImGui::SetCurrentContext(m_context);
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

bool ImGuiOverlay::wantsKeyboard() const {
    ImGui::SetCurrentContext(m_context);
    return m_visible && ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiOverlay::wantsMouse() const {
    ImGui::SetCurrentContext(m_context);
    return m_visible && ImGui::GetIO().WantCaptureMouse;
}

void ImGuiOverlay::draw() {
    if (!m_visible) {
        // Draw data a skipped hook never recorded must not be drawn once it is shown again.
        m_gpuPending = false;
        return;
    }
    // Another overlay may have its context current: the frame is built in this one's own.
    ImGui::SetCurrentContext(m_context);
    if (m_gpuStarted) {
        ImGui_ImplSDLGPU3_NewFrame();
    } else {
        ImGui_ImplOpenGL3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    // The display's size is known now (the backend just read the window): size the widgets and the font for it, so a
    // resized window or a new display is followed from the next frame on.
    m_scale.update(ImGui::GetIO().DisplaySize.y);
    m_view.setUiScale(m_scale.scale());
    ImGui::NewFrame();
    m_view.draw(kHideLabel);
    ImGui::Render();
    if (m_gpuStarted) {
        // Recorded by the renderer's callback, into the frame it is still putting together.
        m_gpuPending = true;
    } else {
        // The OpenGL 3 backend saves the GL state it changes and restores it afterwards.
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    if (m_hideAfter > 0) {
        --m_hideAfter;
        m_visible = m_hideAfter > 0;
    }
}

void ImGuiOverlay::recordGpu(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target) {
    if (!m_gpuPending) {
        return;
    }
    m_gpuPending = false;
    // Another overlay's context may be the current one by now: draw this one's own data.
    ImGui::SetCurrentContext(m_context);
    ImDrawData* data = ImGui::GetDrawData();
    if (data == nullptr || data->TotalVtxCount == 0) {
        return;
    }
    // The backend's buffers and textures are uploaded in a copy pass, which cannot be inside the render pass.
    ImGui_ImplSDLGPU3_PrepareDrawData(data, commands);
    SDL_GPUColorTargetInfo colour{};
    colour.texture = target;
    colour.load_op = SDL_GPU_LOADOP_LOAD; // over the 2D passes
    colour.store_op = SDL_GPU_STOREOP_STORE;
    if (SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &colour, 1, nullptr); pass != nullptr) {
        ImGui_ImplSDLGPU3_RenderDrawData(data, commands, pass);
        SDL_EndGPURenderPass(pass);
    }
}

} // namespace coney::platform
