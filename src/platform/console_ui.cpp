// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/console_ui.h"

#include <utility>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

namespace coney::platform {

namespace {

// The bar's settings file's name, which coney.release.has_console_bar finds in the release executable.
constexpr const char* kSettingsFile = "coney-console.ini";

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

// Makes the bar's context current while it lives and puts the one before back, so the debug tools' overlay keeps
// its own context current between the bar's calls.
class ConsoleUi::ContextScope {
  public:
    // Makes `context` current, keeping the one current before.
    explicit ContextScope(ImGuiContext* context) : m_previous(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    // Puts the context current before back.
    ~ContextScope() { ImGui::SetCurrentContext(m_previous); }
    // Neither copied nor moved: it belongs to the scope it was made in.
    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;
    ContextScope(ContextScope&&) = delete;
    ContextScope& operator=(ContextScope&&) = delete;

  private:
    ImGuiContext* m_previous;
};

std::optional<std::string> consoleSettingsPath() {
    char* folder = SDL_GetPrefPath("Coney", "Coney");
    if (folder == nullptr) {
        return std::nullopt;
    }
    std::string path = folder;
    SDL_free(folder);
    return path + kSettingsFile;
}

ConsoleEventRoute routeConsoleEvent(const void* event, bool open, bool testMode) {
    const auto& sdlEvent = *static_cast<const SDL_Event*>(event);
    ConsoleEventRoute route;
    if (sdlEvent.type == SDL_EVENT_KEY_DOWN && !sdlEvent.key.repeat) {
        // F8 opens and closes it, except in test mode, which reads no device.
        if (sdlEvent.key.key == SDLK_F8 && !testMode) {
            route.action = ConsoleEventRoute::Action::Toggle;
            return route;
        }
        // Escape closes it, and does not quit the game while it is open.
        if (sdlEvent.key.key == SDLK_ESCAPE && open) {
            route.action = ConsoleEventRoute::Action::Close;
            return route;
        }
    }
    // A closed bar never begins a Dear ImGui frame, the only place its input queue drains: show it nothing, or the
    // queue grows for as long as the game runs and replays into the line when F8 opens it.
    if (!open) {
        return route;
    }
    route.forward = true;
    route.takesKeyboard = isKeyboardEvent(sdlEvent);
    route.mouse = isMouseEvent(sdlEvent);
    return route;
}

ConsoleUiSettings consoleUiSettings(bool testMode) {
    // Asking SDL for the preference folder creates it, so a test-mode run never asks.
    if (testMode) {
        return {.testMode = true, .iniPath = {}};
    }
    return {.testMode = false, .iniPath = consoleSettingsPath().value_or("")};
}

ConsoleUi::ConsoleUi(console::Console& console, console::ConsolePad& pad, ConsoleUiSettings settings)
    : m_pad(pad), m_settings(std::move(settings)), m_view(console, pad), m_scale(m_scaleChoice, m_scaleApplied) {}

std::expected<std::unique_ptr<ConsoleUi>, Error> ConsoleUi::start(RenderEngine& renderer, console::Console& console,
                                                                  console::ConsolePad& pad,
                                                                  ConsoleUiSettings settings) {
    const auto [window, context] = renderer.sdlWindowAndContext();
    if (window == nullptr || context == nullptr) {
        return fail(ErrorCode::PlatformFailure, "the console bar needs a window with an OpenGL context");
    }
    // A test-mode run reads and writes no settings file (docs/guides/studio/console.md#test-mode).
    if (settings.testMode) {
        settings.iniPath.clear();
    }
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<ConsoleUi> ui(new ConsoleUi(console, pad, std::move(settings)));
    // Its own context, made current only while the bar works in it, so the debug tools' overlay keeps its own.
    ui->m_context = ImGui::CreateContext();
    const ContextScope scope(ui->m_context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = ui->m_settings.iniPath.empty() ? nullptr : ui->m_settings.iniPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ui->m_view.addSettingsHandler();
    ui->m_scale.setup();
    // From here on the destructor undoes whatever started, so an early return cleans up.
    ui->m_sdlStarted = ImGui_ImplSDL3_InitForOpenGL(static_cast<SDL_Window*>(window), context);
    if (!ui->m_sdlStarted) {
        return fail(ErrorCode::PlatformFailure, "Dear ImGui's SDL3 backend did not start for the console bar");
    }
    ui->m_glStarted = ImGui_ImplOpenGL3_Init(nullptr);
    if (!ui->m_glStarted) {
        return fail(ErrorCode::PlatformFailure, "Dear ImGui's OpenGL 3 backend did not start for the console bar");
    }
    // Read now, so a pinned terminal shows from the first frame.
    if (io.IniFilename != nullptr) {
        ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    }
    return ui;
}

// Undoes whatever start() got as far as, in reverse order, in the bar's own context.
ConsoleUi::~ConsoleUi() {
    if (m_context == nullptr) {
        return;
    }
    {
        const ContextScope scope(m_context);
        if (m_glStarted) {
            ImGui_ImplOpenGL3_Shutdown();
        }
        if (m_sdlStarted) {
            ImGui_ImplSDL3_Shutdown();
        }
    }
    ImGui::DestroyContext(m_context);
}

bool ConsoleUi::handleEvent(const void* event) {
    const ConsoleEventRoute route = routeConsoleEvent(event, m_pad.open(), m_settings.testMode);
    switch (route.action) {
    case ConsoleEventRoute::Action::Toggle:
        setOpen(!m_pad.open());
        return true;
    case ConsoleEventRoute::Action::Close:
        setOpen(false);
        return true;
    case ConsoleEventRoute::Action::None:
        break;
    }
    if (!route.forward) {
        return false;
    }
    // Open: Dear ImGui sees the event, and every frame drawn drains it.
    const ContextScope scope(m_context);
    ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(event));
    return route.takesKeyboard || (route.mouse && ImGui::GetIO().WantCaptureMouse);
}

void ConsoleUi::setOpen(bool open) {
    if (open && !m_pad.open()) {
        m_view.focusLine();
    }
    m_pad.setOpen(open, console::OpenedBy::Keyboard);
    m_hideAfter = 0;
}

void ConsoleUi::showForFrames(std::uint64_t frames) {
    setOpen(frames > 0);
    m_hideAfter = frames;
}

void ConsoleUi::draw() {
    const bool justOpened = m_pad.open() && !m_wasOpen;
    m_wasOpen = m_pad.open();
    if (!m_pad.open() && !m_view.pinned()) {
        return; // nothing to draw: the frame stays as the game left it
    }
    const ContextScope scope(m_context);
    // Keys Dear ImGui saw go down while it was open and up while it was closed (closed it sees nothing) would stay held
    // and repeat into the line, so it starts each opening with none held.
    if (justOpened) {
        ImGui::GetIO().ClearInputKeys();
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    // Sized for the display now that the backend has read the window (docs/guides/debug-menu.md#ui-scale).
    m_scale.update(ImGui::GetIO().DisplaySize.y);
    ImGui::NewFrame();
    m_view.draw();
    ImGui::Render();
    // The OpenGL 3 backend saves the GL state it changes and restores it afterwards.
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (m_hideAfter > 0) {
        --m_hideAfter;
        if (m_hideAfter == 0) {
            m_pad.setOpen(false);
        }
    }
}

} // namespace coney::platform
