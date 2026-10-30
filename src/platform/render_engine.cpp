// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/render_engine.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <rw.h>

#include "core/assert.h"
#include "core/perf.h"
#include "core/window_display.h"
#include "platform/character_skin.h"
#include "platform/gpu_presenter.h"
#include "platform/movie_screen.h"
#include "platform/perf_probes.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_lod.h"
#include "platform/world_atomic.h"

namespace coney::platform {

namespace {

// The error for a start-up step that refused. SDL's own error text is added when `withSdlError` is set: librw's GL3
// device reports its SDL failures only through SDL.
std::unexpected<Error> startFailure(std::string_view step, bool withSdlError) {
    std::string message = "could not start the renderer: " + std::string(step) + " failed";
    if (withSdlError) {
        const char* sdl = SDL_GetError();
        if (sdl != nullptr && *sdl != '\0') {
            message += std::string(": ") + sdl;
        }
    }
    return std::unexpected(Error{ErrorCode::PlatformFailure, std::move(message)});
}

// librw's colour type from ours.
rw::RGBA toRw(graphics::Rgba colour) { return rw::makeRGBA(colour.r, colour.g, colour.b, colour.a); }

// Stands in for glDeleteTextures under the NULL backend. Every librw raster carries the GL3 platform's extension, whose
// destructor deletes the raster's GL texture (id 0 when it never had one) whatever the raster's own platform; with no
// GL context the loader's pointer is null and that call would crash. Texture rasters are the only ones the NULL
// backend makes (PS2 rasters from texture dictionaries), and glDeleteTextures is the only GL call their destructor
// makes.
void APIENTRY deleteNoTextures(GLsizei /*count*/, const GLuint* /*textures*/) {}

// The loader's glDeleteTextures from before a NULL engine started, put back when it stops. Only one engine runs at a
// time.
PFNGLDELETETEXTURESPROC savedDeleteTextures = nullptr;

// The leading "major.minor" of an OpenGL version string, as major * 10 + minor; 0 when it does not start with one.
int glVersionOf(std::string_view text) {
    const std::size_t dot = text.find('.');
    if (dot == 0 || dot == std::string_view::npos || dot + 1 >= text.size()) {
        return 0;
    }
    int major = 0;
    for (const char c : text.substr(0, dot)) {
        if (c < '0' || c > '9') {
            return 0;
        }
        major = (major * 10) + (c - '0');
    }
    const char minor = text[dot + 1];
    if (minor < '0' || minor > '9') {
        return 0;
    }
    return (major * 10) + (minor - '0');
}

// The version of the OpenGL context librw's GL3 device is about to get, when it is under the 3.3 that device draws
// with; nothing when it is enough or no context could be made (librw's own attempt then fails and says so). librw asks
// SDL for a 3.3 core context and never checks what it got, and on Windows SDL hands back an old context when the
// driver cannot make a 3.3 one (Windows' own OpenGL 1.1 renderer, on a machine with no graphics driver, such as CI's):
// librw then calls 3.3 functions the context does not have and crashes. So a hidden window makes the same request
// first and reads the version back.
std::optional<std::string> openGlTooOld() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window* window = SDL_CreateWindow("", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        return std::nullopt;
    }
    std::optional<std::string> tooOld;
    if (SDL_GLContext context = SDL_GL_CreateContext(window); context != nullptr) {
        // SDL hands every GL function back untyped.
        const auto getString = reinterpret_cast<PFNGLGETSTRINGPROC>(SDL_GL_GetProcAddress("glGetString"));
        const GLubyte* version = getString != nullptr ? getString(GL_VERSION) : nullptr;
        const std::string_view text = version != nullptr ? reinterpret_cast<const char*>(version) : "";
        if (glVersionOf(text) < 33) {
            tooOld = text.empty() ? std::string("unknown") : std::string(text);
        }
        SDL_GL_DestroyContext(context);
    }
    SDL_DestroyWindow(window);
    return tooOld;
}

// The texture units librw keeps a cache of (its MAXNUMSTAGES).
constexpr int kTextureStages = 8;

// Makes the real OpenGL state of a fresh context agree with what librw believes after its start: its white texture
// bound on every texture unit. librw's start binds white on each unit through a cache that skips the call when the
// cached name equals the one asked for, and the cache outlives the last renderer, so in a restarted renderer the white
// texture (which gets the first name again) matched and units 1 to 7 stayed on texture 0. Unit 0 is where librw leaves
// its active unit, so what it has cached there is its white texture; the other units are bound directly, and unit 0
// is made active again for librw's own cache of the active unit.
void bindWhiteTextureOnEveryUnit() {
    GLint activeUnit = -1;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);
    CONEY_ASSERT(activeUnit == static_cast<GLint>(GL_TEXTURE0)); // where librw's start leaves it
    const GLuint white = rw::gl3::bindTexture(0);
    CONEY_ASSERT(white != 0); // librw's start bound its white texture here
    rw::gl3::bindTexture(white);
    for (int unit = 1; unit < kTextureStages; ++unit) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, white);
    }
    glActiveTexture(GL_TEXTURE0);
}

// The rectangle of an SDL one.
DisplayRect toDisplayRect(const SDL_Rect& rect) { return {rect.x, rect.y, rect.w, rect.h}; }

// The displays SDL lists, in its order (the primary first), with the ids to address them by.
std::vector<std::pair<SDL_DisplayID, DisplayInfo>> listDisplays() {
    std::vector<std::pair<SDL_DisplayID, DisplayInfo>> displays;
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    for (int i = 0; ids != nullptr && i < count; ++i) {
        DisplayInfo info;
        const char* name = SDL_GetDisplayName(ids[i]);
        info.name = name != nullptr ? name : "";
        SDL_Rect rect{};
        SDL_GetDisplayBounds(ids[i], &rect);
        info.bounds = toDisplayRect(rect);
        if (!SDL_GetDisplayUsableBounds(ids[i], &rect)) {
            rect = SDL_Rect{info.bounds.x, info.bounds.y, info.bounds.width, info.bounds.height};
        }
        info.usable = toDisplayRect(rect);
        displays.emplace_back(ids[i], std::move(info));
    }
    SDL_free(ids);
    return displays;
}

// Where the display setting comes from (`--window-display`, else CONEY_WINDOW_DISPLAY) and its text; empty when
// neither is set.
struct DisplaySetting {
    std::string_view source;
    std::string_view value;
};

// The display setting in force: the option when given, else the environment variable.
DisplaySetting displaySetting(std::string_view option) {
    if (!option.empty()) {
        return {"--window-display", option};
    }
    const char* fromEnvironment = SDL_getenv("CONEY_WINDOW_DISPLAY");
    return {"CONEY_WINDOW_DISPLAY",
            fromEnvironment != nullptr ? std::string_view(fromEnvironment) : std::string_view()};
}

// The display the setting names (core/window_display.h); nothing when it is unset or nothing matches. A value that
// matches nothing is reported once per process on stderr, with the displays there are, and the window then opens
// where SDL puts it.
std::optional<SDL_DisplayID> chosenDisplay(std::string_view option) {
    const auto [source, value] = displaySetting(option);
    const std::optional<WindowDisplayChoice> choice = parseWindowDisplay(value);
    if (!choice) {
        return std::nullopt;
    }
    const std::vector<std::pair<SDL_DisplayID, DisplayInfo>> displays = listDisplays();
    std::vector<DisplayInfo> infos;
    infos.reserve(displays.size());
    for (const auto& [id, info] : displays) {
        infos.push_back(info);
    }
    if (const std::optional<std::size_t> index = pickDisplay(*choice, infos)) {
        return displays[*index].first;
    }
    static bool warned = false;
    if (!warned) {
        warned = true;
        std::string known;
        for (std::size_t i = 0; i < infos.size(); ++i) {
            known += std::format("{}{} \"{}\" {}x{}", i == 0 ? "" : ", ", i + 1, infos[i].name, infos[i].bounds.width,
                                 infos[i].bounds.height);
        }
        std::fprintf(stderr,
                     "coney: warning: %.*s \"%.*s\" matches no display (there are: %s); the window opens where "
                     "the system puts it\n",
                     static_cast<int>(source.size()), source.data(), static_cast<int>(value.size()), value.data(),
                     known.c_str());
    }
    return std::nullopt;
}

// Centres `window` on `display`'s usable area, its frame included, shrinking it to fit when it is larger. The size is
// the client area's, so the borders the window system adds round it are taken off the area first.
void placeOnDisplay(SDL_Window* window, SDL_DisplayID display) {
    SDL_Rect usable{};
    if (!SDL_GetDisplayUsableBounds(display, &usable)) {
        return;
    }
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    if (!SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right)) {
        top = left = bottom = right = 0; // a window system that cannot say: the client area alone
    }
    const DisplayRect area{usable.x + left, usable.y + top, usable.w - left - right, usable.h - top - bottom};
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    const DisplayRect place = placeWindow(area, width, height);
    if (place.width != width || place.height != height) {
        SDL_SetWindowSize(window, place.width, place.height);
    }
    SDL_SetWindowPosition(window, place.x, place.y);
    SDL_SyncWindow(window); // window systems that move windows later (X11) have done it before the size is read
    // A display with another scale changes the client area on the way: Windows rescales the window as it crosses
    // (measured: 640x448 made on a 150 % display came out 646x465 on a 100 % one). The frame is the client area, so
    // a grown one letterboxes the logical screen and no longer matches `--render-size`; set the size again, and
    // centre it again at that size.
    int placedWidth = 0;
    int placedHeight = 0;
    SDL_GetWindowSize(window, &placedWidth, &placedHeight);
    if (placedWidth != place.width || placedHeight != place.height) {
        SDL_SetWindowSize(window, place.width, place.height);
        SDL_SetWindowPosition(window, place.x, place.y);
        SDL_SyncWindow(window);
    }
}

// What the event watch below needs: whether to hide the window, which display to move it to (none: where it is), and
// whether it has been dealt with.
struct DisplayWatch {
    SDL_DisplayID display = 0;
    bool hide = false;
    bool placed = false;
};

// Hides librw's window, and moves it to its display, the moment SDL shows it. librw's GL3 device makes and shows the
// window inside Engine::start with SDL's default position (the primary display) and offers no way to make it hidden,
// so this watch, which SDL calls as it sends the window's SHOWN event, is the earliest point Coney can reach it:
// before its OpenGL context exists and before any frame is drawn. A window that is to stay hidden is hidden here, so
// the screen sees it for no longer than SDL's own show call takes; it is hidden before it is moved. The OpenGL
// check's own hidden window is never shown, so it is never touched.
bool SDLCALL placeWhenShown(void* userdata, SDL_Event* event) {
    auto* watch = static_cast<DisplayWatch*>(userdata);
    if (!watch->placed && event->type == SDL_EVENT_WINDOW_SHOWN) {
        if (SDL_Window* window = SDL_GetWindowFromID(event->window.windowID); window != nullptr) {
            watch->placed = true;
            if (watch->hide) {
                SDL_HideWindow(window);
            }
            if (watch->display != 0) {
                placeOnDisplay(window, watch->display);
            }
        }
    }
    return true;
}

} // namespace

RenderEngine::RenderEngine(RenderBackend backend, const WindowDesc& desc)
    : m_backend(backend), m_title(desc.title), m_frameSize{desc.width, desc.height}, m_logicalFrame(desc.logicalFrame) {
}

float RenderEngine::viewAspect() const {
    if (m_logicalFrame || m_frameSize.height <= 0) {
        return static_cast<float>(graphics::kStandardAspect.width) /
               static_cast<float>(graphics::kStandardAspect.height);
    }
    return static_cast<float>(m_frameSize.width) / static_cast<float>(m_frameSize.height);
}

// Where the logical screen goes in a frame of `size`: all of it when the frame is the logical screen, else fitted.
graphics::ScreenRect RenderEngine::logicalScreenIn(graphics::Extent size) const {
    return m_logicalFrame ? graphics::ScreenRect{0, 0, size.width, size.height} : graphics::fitLogicalScreen(size);
}

RenderEngine::~RenderEngine() { shutDown(); }

void setWindowActivation(bool activate) {
    // SDL shows a window without activating it (SWP_NOACTIVATE on Windows) when ACTIVATE_WHEN_SHOWN is off, and
    // ACTIVATE_WHEN_RAISED keeps a later raise from bringing it to the front. librw's GL3 device shows the window
    // it makes with SDL_ShowWindow's defaults, so these hints are the only way to reach it.
    const char* value = activate ? "1" : "0";
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, value);
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, value);
}

std::expected<std::unique_ptr<RenderEngine>, Error> RenderEngine::start(RenderBackend backend, const WindowDesc& desc) {
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<RenderEngine> engine(new RenderEngine(backend, desc));
    const bool openGl = backend == RenderBackend::OpenGl;
    const bool gpu = backend == RenderBackend::Gpu;

    // A window (OpenGL, GPU): start SDL's video first and check for a display. librw's GL3 device does start SDL's
    // video itself, but it ignores its own failures and goes on to use a display list it never made; holding our own
    // reference also keeps SDL alive until librw has destroyed its window, and the GPU backend's window lives on it.
    if (openGl || gpu) {
        // Before librw makes and shows the window, so a run nobody is playing never takes the keyboard focus. A run
        // placed on a display of its own (--window-display, CONEY_WINDOW_DISPLAY) is one an agent or tool started, so
        // it never takes it either, on this show or a later one.
        const bool placed = parseWindowDisplay(displaySetting(desc.display).value).has_value();
        setWindowActivation(desc.activate && !desc.hidden && !placed);
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            return startFailure("SDL_InitSubSystem(SDL_INIT_VIDEO)", true);
        }
        engine->m_sdlStarted = true;
        int displays = 0;
        SDL_DisplayID* list = SDL_GetDisplays(&displays);
        SDL_free(list);
        if (displays <= 0) {
            return startFailure("finding a display (run with --headless where there is none)", true);
        }
    }
    if (openGl) {
        if (const std::optional<std::string> version = openGlTooOld()) {
            return std::unexpected(Error{ErrorCode::PlatformFailure,
                                         "could not start the renderer: it needs OpenGL 3.3, and the driver offers " +
                                             *version + " (run with --headless where there is none)"});
        }
    }

    // GPU: SDL's device and Coney's own window, claimed for it on this thread (gpu_presenter.h), before librw, so a
    // machine with no Vulkan, Direct3D 12 or Metal fails before anything else starts; the window shows further down.
    if (gpu) {
        GpuPresenter::Desc gpuDesc;
        gpuDesc.title = engine->m_title;
        gpuDesc.width = desc.width;
        gpuDesc.height = desc.height;
        gpuDesc.vsync = engine->m_vsync;
        gpuDesc.fpsCap = engine->m_fpsCap;
        auto presenter = GpuPresenter::start(gpuDesc);
        if (!presenter) {
            return std::unexpected(
                Error{ErrorCode::PlatformFailure, "could not start the GPU renderer: " + presenter.error().message});
        }
        engine->m_gpu = std::move(*presenter);
        engine->m_sdlWindow = engine->m_gpu->window();
    }

    // librw's three start-up steps. The default memory functions (malloc and free) are fine until Coney has an
    // allocator of its own.
    if (!rw::Engine::init()) {
        return startFailure("librw Engine::init", false);
    }
    // Plugins are registered between init and open, as librw requires: the streamed world's (platform/world_atomic.h).
    attachWorldPlugins();
    // Each converted texture's GS level parameters, for drawing at the original's mip levels (platform/texture_lod.h).
    attachTextureLodPlugin();
    // Characters skinned in the vertex shader: their weights, bone matrices and the shader's uniform
    // (platform/character_skin.h).
    attachCharacterSkin();
    // Anisotropic filtering per texture, which the sandbox's grid floor needs to stay sharp at grazing angles
    // (platform/sandbox_renderer.h); it only takes effect on textures that ask for it.
    rw::registerAnisotropyPlugin();
    rw::EngineOpenParams params{};
    params.window = reinterpret_cast<SDL_Window**>(&engine->m_sdlWindow);
    params.fullscreen = 0;
    params.width = desc.width;
    params.height = desc.height;
    params.windowtitle = engine->m_title.c_str();
    // Engine::open copies the device for this platform (always GL3 in Coney's build) into the engine and opens it.
    // For the NULL and GPU backends, put librw's NULL device in that slot for the duration of the copy, then put GL3
    // back: the engine then runs on the NULL device until it closes, for data only, and never touches SDL's video.
    bool opened = false;
    // `--window-display` / CONEY_WINDOW_DISPLAY, and a window that is to stay hidden: librw's GL3 device makes and
    // shows the window in Engine::start, so the watch covers open and start, and the window is hidden and goes to its
    // display as it is shown (placeWhenShown), or right after start on a window system that never sent the event. The
    // guard removes the watch on every way out.
    DisplayWatch displayWatch;
    std::optional<SDL_DisplayID> display;
    struct WatchGuard {
        DisplayWatch* watch = nullptr;
        WatchGuard() = default;
        WatchGuard(const WatchGuard&) = delete;
        WatchGuard& operator=(const WatchGuard&) = delete;
        WatchGuard(WatchGuard&&) = delete;
        WatchGuard& operator=(WatchGuard&&) = delete;
        ~WatchGuard() {
            if (watch != nullptr) {
                SDL_RemoveEventWatch(placeWhenShown, watch);
            }
        }
    } watchGuard;
    if (openGl) {
        display = chosenDisplay(desc.display);
        displayWatch.display = display.value_or(0);
        displayWatch.hide = desc.hidden;
        if ((display || desc.hidden) && SDL_AddEventWatch(placeWhenShown, &displayWatch)) {
            watchGuard.watch = &displayWatch;
        }
        opened = rw::Engine::open(&params) != 0;
    } else {
        const rw::Device gl3Device = rw::gl3::renderdevice;
        rw::gl3::renderdevice = rw::null::renderdevice;
        opened = rw::Engine::open(&params) != 0;
        rw::gl3::renderdevice = gl3Device;
        savedDeleteTextures = glad_glDeleteTextures;
        glad_glDeleteTextures = deleteNoTextures;
        engine->m_glStubbed = true;
    }
    if (!opened) {
        rw::Engine::term();
        return startFailure("librw Engine::open", false);
    }
    if (!rw::Engine::start()) {
        rw::Engine::close();
        rw::Engine::term();
        return startFailure("librw Engine::start", false);
    }
    engine->m_librwStarted = true;
    if (gpu) {
        engine->showGpuWindow(desc);
        return engine;
    }
    if (!openGl) {
        return engine;
    }
    if (display && !displayWatch.placed && engine->m_sdlWindow != nullptr) {
        placeOnDisplay(static_cast<SDL_Window*>(engine->m_sdlWindow), *display);
    }

    // Engine::start succeeds even when the GL3 device could not make a window or a context; the only sign is that it
    // never wrote the window back. Then GL was never set up, so the engine must not stop through the GL3 device (its
    // stop step deletes GL objects): switch it to the NULL device, which stops cleanly, and balance the reference to
    // SDL's video that the GL3 device's open step took, which the NULL device's close step will not release.
    if (engine->m_sdlWindow == nullptr) {
        auto failure = startFailure("creating an OpenGL window and context", true);
        rw::engine->device = rw::null::renderdevice;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return failure; // the destructor stops librw and releases our own reference to SDL
    }
    // librw's GL3 device always shows the window it makes; a tool that draws only into offscreen buffers hides it
    // again (the OpenGL context stays usable). The watch above already did, as the window was shown; this is for a
    // window system that never sent the event.
    if (desc.hidden) {
        SDL_HideWindow(static_cast<SDL_Window*>(engine->m_sdlWindow));
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(static_cast<SDL_Window*>(engine->m_sdlWindow), &width, &height);
    engine->m_frameSize = graphics::Extent{width, height};
    bindWhiteTextureOnEveryUnit();
    // librw's start sets the fog colour to white in its state but not in the registry its shaders read, and marks
    // nothing changed: shaders made now would upload the colour the last renderer left there. Setting it again marks it
    // changed, so the first draw puts white in the registry (also in a first renderer, where the registry holds zeros).
    rw::SetRenderState(rw::FOGCOLOR, 0xFFFFFFFFU);
    engine->createCamera();
    // Mip levels by distance, as the GS picks them; without it OpenGL picks them by screen size.
    engine->m_textureLod = startTextureLod();
    // Characters skinned on the GPU, drawn through the same fragment step; CPU skinning where it cannot start.
    if (engine->m_textureLod) {
        startCharacterSkin();
    }
    // The profiler's counts of draws, uploads and state changes (docs/guides/performance.md).
    installGlCounters();
    return engine;
}

void RenderEngine::showGpuWindow(const WindowDesc& desc) {
    // The window is Coney's own, made hidden, so it goes to its display (`--window-display`) before it is ever seen,
    // and a window that is to stay hidden is never shown at all.
    auto* window = static_cast<SDL_Window*>(m_sdlWindow);
    if (const std::optional<SDL_DisplayID> display = chosenDisplay(desc.display); display) {
        placeOnDisplay(window, *display);
    }
    if (!desc.hidden) {
        SDL_ShowWindow(window);
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (width > 0 && height > 0) {
        m_frameSize = graphics::Extent{width, height};
    }
}

std::string_view RenderEngine::gpuDriver() const {
    return m_gpu ? std::string_view(m_gpu->driver()) : std::string_view();
}

PresentMode RenderEngine::presentMode() const { return m_gpu ? m_gpu->mode() : PresentMode::Vsync; }

PresentModes RenderEngine::offeredPresentModes() const { return m_gpu ? m_gpu->offered() : PresentModes{}; }

std::optional<Window> RenderEngine::window() const {
    if (m_sdlWindow == nullptr) {
        return std::nullopt;
    }
    return Window(m_sdlWindow);
}

std::pair<void*, void*> RenderEngine::sdlWindowAndContext() const {
    if (m_sdlWindow == nullptr) {
        return {nullptr, nullptr};
    }
    if (m_backend != RenderBackend::OpenGl) {
        return {m_sdlWindow, nullptr}; // the GPU backend's window has no OpenGL context
    }
    // librw's GL3 device keeps its context current on this thread for the engine's whole life.
    return {m_sdlWindow, SDL_GL_GetCurrentContext()};
}

void RenderEngine::createCamera() {
    // As librw's own examples do: a camera with a frame (its position, unused by 2D drawing but required by
    // beginUpdate) and frame and depth buffers the size of the window.
    m_camera = rw::Camera::create();
    m_camera->setFrame(rw::Frame::create());
    m_camera->frameBuffer = rw::Raster::create(m_frameSize.width, m_frameSize.height, 0, rw::Raster::CAMERA);
    m_camera->zBuffer = rw::Raster::create(m_frameSize.width, m_frameSize.height, 0, rw::Raster::ZBUFFER);
}

void RenderEngine::destroyCamera() noexcept {
    for (rw::Raster** raster : {&m_lineRaster, &m_blurRaster, &m_halfRaster}) {
        if (*raster != nullptr) {
            (*raster)->destroy();
            *raster = nullptr;
        }
    }
    if (m_camera == nullptr) {
        return;
    }
    // Buffers and frame first: Camera::destroy leaves them alone.
    if (m_camera->frameBuffer != nullptr) {
        m_camera->frameBuffer->destroy();
        m_camera->frameBuffer = nullptr;
    }
    if (m_camera->zBuffer != nullptr) {
        m_camera->zBuffer->destroy();
        m_camera->zBuffer = nullptr;
    }
    if (rw::Frame* frame = m_camera->getFrame(); frame != nullptr) {
        m_camera->setFrame(nullptr);
        frame->destroy();
    }
    m_camera->destroy();
    m_camera = nullptr;
}

void RenderEngine::beginFrame(graphics::Rgba clear) {
    // The border around the logical screen is black; the logical screen itself is filled with `clear` by a flat quad,
    // since librw's camera clear always covers the whole frame buffer.
    startFrame(graphics::kBlack);
    m_clearColour = clear;
    if (m_camera == nullptr || clear == graphics::kBlack) {
        return;
    }
    const graphics::LogicalQuad fill{0.0F, 0.0F, graphics::kLogicalWidth, graphics::kLogicalHeight, graphics::UvRect{},
                                     clear};
    drawQuads(nullptr, std::span(&fill, 1));
}

void RenderEngine::beginWindowFrame(graphics::Rgba clear) { startFrame(clear); }

void RenderEngine::drawQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads) {
    drawTexturedQuads(texture, quads, false, m_viewport);
}

void RenderEngine::drawWrappedQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads) {
    drawTexturedQuads(texture, quads, true, m_viewport);
}

void RenderEngine::drawViewQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads) {
    drawTexturedQuads(texture, quads, false, viewRect());
}

void RenderEngine::drawWrappedViewQuads(const graphics::Texture* texture,
                                        std::span<const graphics::LogicalQuad> quads) {
    drawTexturedQuads(texture, quads, true, viewRect());
}

rw::Raster* RenderEngine::bindTexture(const graphics::Texture* texture, bool wrap) {
    if (texture == nullptr) {
        return nullptr;
    }
    // A sprite sheet's texture, or a movie's frame (platform/movie_screen.h).
    rw::Texture* rwTexture = nullptr;
    if (const auto* sheetTexture = dynamic_cast<const SheetTexture*>(texture); sheetTexture != nullptr) {
        rwTexture = sheetTexture->rwTexture();
    } else if (const auto* movieTexture = dynamic_cast<const MovieTexture*>(texture); movieTexture != nullptr) {
        rwTexture = movieTexture->rwTexture();
    }
    CONEY_ASSERT(rwTexture != nullptr);
    // The texture's own filtering (most of the game's textures ask for linear), clamped at the edges so a rectangle
    // that reaches the texture's border does not pick up texels from the opposite side, unless the caller repeats the
    // texture.
    rw::SetRenderState(rw::TEXTUREFILTER, rwTexture->getFilter());
    rw::SetRenderState(rw::TEXTUREADDRESS, wrap ? rw::Texture::WRAP : rw::Texture::CLAMP);
    return rwTexture->raster;
}

void RenderEngine::drawTriangles(const graphics::Texture* texture, std::span<const graphics::LogicalVertex> vertices,
                                 const graphics::TriangleStates& states) {
    CONEY_ASSERT(m_inFrame);
    const std::size_t count = vertices.size() / 3 * 3;
    if (m_camera == nullptr || count == 0) {
        return; // NULL backend: nothing to draw
    }
    rw::Raster* raster = bindTexture(texture, states.wrap);
    set2dStates(raster);
    // The alpha test, as the GS's: keep a fragment whose alpha is at least the reference.
    const rw::uint32 testFunction = rw::GetRenderState(rw::ALPHATESTFUNC);
    const rw::uint32 testRef = rw::GetRenderState(rw::ALPHATESTREF);
    if (states.alphaRef > 0.0F) {
        rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
        rw::SetRenderState(rw::ALPHATESTREF, static_cast<rw::uint32>(std::lround(states.alphaRef * 255.0F)));
    } else {
        rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS); // every fragment kept, however faint
    }
    // Logical pixels to window pixels: a point is a rectangle of no size.
    const float nearZ = rw::im2d::GetNearZ();
    const float recipZ = 1.0F / m_camera->nearPlane;
    std::vector<rw::gl3::Im2DVertex> mapped(count);
    for (std::size_t i = 0; i < count; ++i) {
        const graphics::LogicalVertex& in = vertices[i];
        const graphics::LogicalRect at =
            graphics::logicalToWindow(graphics::LogicalRect{in.x, in.y, 0.0F, 0.0F}, m_viewport);
        rw::gl3::Im2DVertex& vertex = mapped[i];
        vertex.setScreenX(at.x);
        vertex.setScreenY(at.y);
        vertex.setScreenZ(nearZ);
        vertex.setRecipCameraZ(recipZ);
        vertex.setColor(in.colour.r, in.colour.g, in.colour.b, in.colour.a);
        vertex.setU(in.u, recipZ);
        vertex.setV(in.v, recipZ);
    }
    rw::im2d::RenderPrimitive(rw::PRIMTYPETRILIST, mapped.data(), static_cast<rw::int32>(count));
    rw::SetRenderState(rw::ALPHATESTFUNC, testFunction);
    rw::SetRenderState(rw::ALPHATESTREF, testRef);
}

void RenderEngine::drawTexturedQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads,
                                     bool wrap, const graphics::ScreenRect& onto) {
    CONEY_ASSERT(m_inFrame);
    if (m_camera == nullptr || quads.empty()) {
        return; // NULL backend: nothing to draw
    }
    rw::Raster* raster = bindTexture(texture, wrap);
    // Logical pixels to window pixels over `onto`, then the shared 2D drawing.
    std::vector<graphics::LogicalQuad> mapped(quads.begin(), quads.end());
    for (graphics::LogicalQuad& quad : mapped) {
        const graphics::LogicalRect rect =
            graphics::logicalToWindow(graphics::LogicalRect{quad.x, quad.y, quad.width, quad.height}, onto);
        quad.x = rect.x;
        quad.y = rect.y;
        quad.width = rect.width;
        quad.height = rect.height;
    }
    drawWindowQuads(raster, mapped);
}

void RenderEngine::set2dStates(rw::Raster* raster, bool opaque) {
    // The 2D pass's states (docs/research/graphics.md#2d-drawing): no depth test or write, no culling, no fog, blended
    // by the vertex and texture alpha over what is already drawn.
    rw::SetRenderState(rw::ZTESTENABLE, 0);
    rw::SetRenderState(rw::ZWRITEENABLE, 0);
    rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
    rw::SetRenderState(rw::FOGENABLE, 0);
    rw::SetRenderState(rw::VERTEXALPHA, 1);
    rw::SetRenderState(rw::SRCBLEND, opaque ? rw::BLENDONE : rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, opaque ? rw::BLENDZERO : rw::BLENDINVSRCALPHA);
    rw::SetRenderStatePtr(rw::TEXTURERASTER, raster);
}

void RenderEngine::drawWindowQuads(rw::Raster* raster, std::span<const graphics::LogicalQuad> quads, bool opaque) {
    set2dStates(raster, opaque);

    // Four corners and two triangles per quad, clockwise from the top left, as the device's screen quads.
    const float nearZ = rw::im2d::GetNearZ();
    const float recipZ = 1.0F / m_camera->nearPlane;
    std::vector<rw::gl3::Im2DVertex> vertices(quads.size() * 4);
    for (std::size_t q = 0; q < quads.size(); ++q) {
        const graphics::LogicalQuad& quad = quads[q];
        struct Corner {
            float x, y, u, v;
        };
        const std::array<Corner, 4> corners{{{quad.x, quad.y, quad.uv.u0, quad.uv.v0},
                                             {quad.x + quad.width, quad.y, quad.uv.u1, quad.uv.v0},
                                             {quad.x + quad.width, quad.y + quad.height, quad.uv.u1, quad.uv.v1},
                                             {quad.x, quad.y + quad.height, quad.uv.u0, quad.uv.v1}}};
        for (std::size_t i = 0; i < corners.size(); ++i) {
            rw::gl3::Im2DVertex& vertex = vertices[q * 4 + i];
            vertex.setScreenX(corners[i].x);
            vertex.setScreenY(corners[i].y);
            vertex.setScreenZ(nearZ);
            vertex.setRecipCameraZ(recipZ);
            vertex.setColor(quad.colour.r, quad.colour.g, quad.colour.b, quad.colour.a);
            vertex.setU(corners[i].u, recipZ);
            vertex.setV(corners[i].v, recipZ);
        }
    }
    // 16-bit indices: draw in runs of at most 16,384 quads, each run indexing its own vertices from 0.
    constexpr std::size_t kMaxQuadsPerDraw = 0x10000 / 4;
    std::vector<std::uint16_t> indices;
    for (std::size_t first = 0; first < quads.size(); first += kMaxQuadsPerDraw) {
        const std::size_t count = std::min(kMaxQuadsPerDraw, quads.size() - first);
        indices.clear();
        indices.reserve(count * 6);
        for (std::size_t q = 0; q < count; ++q) {
            for (const std::size_t corner : {0U, 1U, 2U, 0U, 2U, 3U}) {
                indices.push_back(static_cast<std::uint16_t>(q * 4 + corner));
            }
        }
        rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, &vertices[first * 4], static_cast<rw::int32>(count * 4),
                                         indices.data(), static_cast<rw::int32>(indices.size()));
    }
}

void RenderEngine::startFrame(graphics::Rgba clear) {
    CONEY_ASSERT(!m_inFrame);
    m_inFrame = true;
    m_clearColour = clear;
    m_viewport = logicalScreenIn(m_frameSize);
    if (m_gpu) {
        // GPU: follow the window's size, as below; the clear itself happens in present().
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(static_cast<SDL_Window*>(m_sdlWindow), &width, &height);
        if (width > 0 && height > 0) {
            m_frameSize = graphics::Extent{width, height};
            m_viewport = logicalScreenIn(m_frameSize);
        }
        return;
    }
    if (m_camera == nullptr) {
        return; // NULL backend: nothing to clear
    }
    // Follow the window's size: librw sizes the camera's viewport from the window, so the buffers must match it.
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(static_cast<SDL_Window*>(m_sdlWindow), &width, &height);
    if (width > 0 && height > 0 && (width != m_frameSize.width || height != m_frameSize.height)) {
        destroyCamera();
        m_frameSize = graphics::Extent{width, height};
        createCamera();
    }
    m_viewport = logicalScreenIn(m_frameSize);
    rw::RGBA colour = toRw(clear);
    m_camera->clear(&colour, rw::Camera::CLEARIMAGE | rw::Camera::CLEARZ);
    m_camera->beginUpdate();
}

void RenderEngine::drawTexture(rw::Texture* texture, graphics::ScreenRect rect) {
    CONEY_ASSERT(m_inFrame && m_camera != nullptr && texture != nullptr);
    // Screen-space 2D: depth tests off, blending by the texture's alpha, nearest-texel sampling so texels stay sharp
    // when a small texture is enlarged.
    rw::SetRenderState(rw::ZTESTENABLE, 0);
    rw::SetRenderState(rw::ZWRITEENABLE, 0);
    rw::SetRenderState(rw::VERTEXALPHA, 1);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    rw::SetRenderState(rw::TEXTUREFILTER, rw::Texture::NEAREST);
    rw::SetRenderState(rw::TEXTUREADDRESS, rw::Texture::CLAMP);
    rw::SetRenderStatePtr(rw::TEXTURERASTER, texture->raster);

    // Two triangles over the rectangle: corners clockwise from the top left, with the texture's corners on them.
    const auto left = static_cast<float>(rect.x);
    const auto top = static_cast<float>(rect.y);
    const auto right = static_cast<float>(rect.x + rect.width);
    const auto bottom = static_cast<float>(rect.y + rect.height);
    struct Corner {
        float x, y, u, v;
    };
    const std::array<Corner, 4> corners{
        {{left, top, 0, 0}, {right, top, 1, 0}, {right, bottom, 1, 1}, {left, bottom, 0, 1}}};
    const float nearZ = rw::im2d::GetNearZ();
    const float recipZ = 1.0F / m_camera->nearPlane;
    std::array<rw::gl3::Im2DVertex, 4> vertices{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        rw::gl3::Im2DVertex& vertex = vertices[i];
        vertex.setScreenX(corners[i].x);
        vertex.setScreenY(corners[i].y);
        vertex.setScreenZ(nearZ);
        vertex.setRecipCameraZ(recipZ);
        vertex.setColor(255, 255, 255, 255);
        vertex.setU(corners[i].u, recipZ);
        vertex.setV(corners[i].v, recipZ);
    }
    std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
    rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, vertices.data(), static_cast<rw::int32>(vertices.size()),
                                     indices.data(), static_cast<rw::int32>(indices.size()));
}

void RenderEngine::drawWindowRects(std::span<const graphics::LogicalQuad> quads) {
    CONEY_ASSERT(m_inFrame);
    if (m_camera == nullptr || quads.empty()) {
        return; // NULL backend: nothing to draw
    }
    drawWindowQuads(nullptr, quads);
}

void RenderEngine::present() {
    CONEY_ASSERT(m_inFrame);
    CONEY_PERF_ZONE("present");
    // The modes' 2D layers for this frame first, taken out first so that a layer may add the next frame's; then the
    // debug menus over them.
    for (const std::function<void(RenderEngine&)>& overlay : std::exchange(m_frameOverlays, {})) {
        overlay(*this);
    }
    // The video output's softening covers everything the game draws, but not the debug menus.
    if (m_lineBlend && m_camera != nullptr) {
        blendLines();
    }
    // A clip takes the frame as the game drew it, before the debug menus go over it.
    if (m_frameTap && m_camera != nullptr) {
        tapFrame();
    }
    if (m_presentOverlay) {
        CONEY_PERF_ZONE("present.debug_overlays");
        m_presentOverlay(*this);
    }
    m_inFrame = false;
    const bool captureThisFrame = m_captureFrame.has_value() && *m_captureFrame == m_presented;
    if (m_camera != nullptr) {
        m_camera->endUpdate();
        // Read the frame back before it is shown: after the buffer swap the back buffer's contents are undefined.
        if (captureThisFrame) {
            m_capture = captureBackBuffer(m_capturePath);
        }
        // librw's GL3 device sets the swap interval from the flag on every show: 1 with it, 0 without.
        CONEY_PERF_ZONE("present.swap");
        m_camera->showRaster(m_vsync ? rw::Raster::FLIPWAITVSYNCH : 0);

    } else if (m_gpu) {
        if (captureThisFrame) {
            m_capture = std::unexpected(Error{ErrorCode::PlatformFailure, "the GPU renderer reads no frames back yet"});
        }
        // Stand-in until the GPU renderer draws (docs/engine/renderer.md#order, item 11): the whole frame is cleared to
        // the frame's colour (beginFrame()'s logical screen, beginWindowFrame()'s window), with no black border round
        // the logical screen. A change of vsync or cap is taken here, between frames.
        m_gpu->choosePresentMode(m_vsync, m_fpsCap);
        CONEY_PERF_ZONE("present.swap");
        if (m_gpu->clearAndPresent(m_clearColour)) {
            ++m_shown;
        }
    } else if (captureThisFrame) {
        m_capture = std::unexpected(Error{ErrorCode::PlatformFailure, "the headless renderer draws no frames"});
    }
    ++m_presented;
}

bool RenderEngine::copyToRaster(rw::Raster*& raster, int x, int y, int width, int height) {
    if (raster != nullptr && (raster->width != width || raster->height != height)) {
        raster->destroy();
        raster = nullptr;
    }
    if (raster == nullptr) {
        raster = rw::Raster::create(width, height, 32, static_cast<rw::int32>(rw::Raster::TEXTURE) | rw::Raster::C8888);
        if (raster == nullptr) {
            return false;
        }
    }
    // GL counts the frame buffer's rows from the bottom. librw's 2D drawing samples the copy with v = 0 at the
    // rectangle's top (measured: drawn that way, the copy shows the screen the right way up).
    auto* native = PLUGINOFFSET(rw::gl3::Gl3Raster, raster, rw::gl3::nativeRasterOffset);
    rw::gl3::bindTexture(native->texid);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, m_frameSize.height - (y + height), width, height);
    return true;
}

void RenderEngine::blurScreen(int passes, float offsetU, float offsetV) {
    CONEY_ASSERT(m_inFrame);
    // The whole picture the 3D view fills, as the original blurs its whole screen (in a wide window, past the logical
    // screen's 4:3 place).
    const graphics::ScreenRect viewport = viewRect();
    if (m_camera == nullptr || viewport.width < 2 || viewport.height < 2) {
        return;
    }
    rw::SetRenderState(rw::TEXTUREFILTER, rw::Texture::LINEAR);
    rw::SetRenderState(rw::TEXTUREADDRESS, rw::Texture::CLAMP);
    const auto left = static_cast<float>(viewport.x);
    const auto top = static_cast<float>(viewport.y);
    const int halfWidth = viewport.width / 2;
    const int halfHeight = viewport.height / 2;
    const graphics::Rgba white{255, 255, 255, 255};

    // 1. The screen at half size, drawn into its own top-left quarter (scratch: the last step covers all of it).
    if (!copyToRaster(m_blurRaster, viewport.x, viewport.y, viewport.width, viewport.height)) {
        return;
    }
    const graphics::LogicalQuad half{
        left, top, static_cast<float>(halfWidth), static_cast<float>(halfHeight), graphics::UvRect{}, white};
    drawWindowQuads(m_blurRaster, std::span<const graphics::LogicalQuad>(&half, 1), true);

    // 2. The passes: the half-size image onto itself with one edge moved in by half the offset, the low edges (u0, v0)
    // for a positive offset and the high edges for a negative one, the sign flipping each pass, resampled linearly.
    // The offsets are in UV units of the original's 512 x 256 texture, whose half-size image is 320 x 224 (inferred:
    // a 640 x 448 screen at half size), so they are scaled to the same share of the image here.
    const float stepU = offsetU / 2.0F * (512.0F / 320.0F);
    const float stepV = offsetV / 2.0F * (256.0F / 224.0F);
    bool lowEdge = true;
    for (int pass = 0; pass < passes; ++pass) {
        if (!copyToRaster(m_halfRaster, viewport.x, viewport.y, halfWidth, halfHeight)) {
            return;
        }
        graphics::LogicalQuad quad = half;
        if (lowEdge) {
            quad.uv.u0 += stepU;
            quad.uv.v0 += stepV;
        } else {
            quad.uv.u1 -= stepU;
            quad.uv.v1 -= stepV;
        }
        drawWindowQuads(m_halfRaster, std::span<const graphics::LogicalQuad>(&quad, 1), true);
        lowEdge = !lowEdge;
    }

    // 3. Stretched back over the whole screen, its edges inset by one texel, opaque.
    if (!copyToRaster(m_halfRaster, viewport.x, viewport.y, halfWidth, halfHeight)) {
        return;
    }
    const float texelU = 1.0F / static_cast<float>(halfWidth);
    const float texelV = 1.0F / static_cast<float>(halfHeight);
    const graphics::LogicalQuad whole{left,
                                      top,
                                      static_cast<float>(viewport.width),
                                      static_cast<float>(viewport.height),
                                      graphics::UvRect{texelU, texelV, 1.0F - texelU, 1.0F - texelV},
                                      white};
    drawWindowQuads(m_halfRaster, std::span<const graphics::LogicalQuad>(&whole, 1), true);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
}

void RenderEngine::blendLines() {
    // The whole picture the 3D view fills: the video output softens everything it sends, so in a wide window the
    // view's sides too, not only the logical screen's 4:3 place (which would leave a visible edge 12.5 % in).
    const graphics::ScreenRect viewport = viewRect();
    if (viewport.width <= 0 || viewport.height <= 0) {
        return;
    }
    // 1. The frame as drawn, copied.
    if (!copyToRaster(m_lineRaster, viewport.x, viewport.y, viewport.width, viewport.height)) {
        return;
    }

    // 2. Laid over itself at half strength, shifted so that each window row shows the frame one original line (448 to
    // the screen's height) below it: the PS2's second read circuit starts one line down (DISPFB2's DBY 1) and the
    // two are mixed half and half (PMODE's ALP 0x80). Past the copy's bottom edge it is clamped, as the original's last
    // line has no line below it. Filtered linearly, so a window that is not a whole
    // number of lines per original line still blends exactly one original line.
    const float shift = graphics::originalLineShare(m_viewport, viewport);
    rw::SetRenderState(rw::TEXTUREFILTER, rw::Texture::LINEAR);
    rw::SetRenderState(rw::TEXTUREADDRESS, rw::Texture::CLAMP);
    const graphics::LogicalQuad quad{static_cast<float>(viewport.x),
                                     static_cast<float>(viewport.y),
                                     static_cast<float>(viewport.width),
                                     static_cast<float>(viewport.height),
                                     graphics::UvRect{0.0F, shift, 1.0F, 1.0F + shift},
                                     graphics::Rgba{255, 255, 255, 128}};
    drawWindowQuads(m_lineRaster, std::span<const graphics::LogicalQuad>(&quad, 1));
}

void RenderEngine::tapFrame() {
    const graphics::ScreenRect rect = m_viewport;
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    const auto width = static_cast<std::size_t>(rect.width);
    const auto height = static_cast<std::size_t>(rect.height);
    const std::size_t rowBytes = width * 4;
    m_tapRows.resize(rowBytes * height);
    m_tapFrame.resize(rowBytes * height);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // OpenGL counts rows from the bottom of the frame; the rectangle is given from the top.
    glReadPixels(rect.x, m_frameSize.height - rect.y - rect.height, rect.width, rect.height, GL_RGBA, GL_UNSIGNED_BYTE,
                 m_tapRows.data());
    if (glGetError() != GL_NO_ERROR) {
        return;
    }
    for (std::size_t y = 0; y < height; ++y) {
        std::memcpy(&m_tapFrame[y * rowBytes], &m_tapRows[(height - 1 - y) * rowBytes], rowBytes);
    }
    for (std::size_t i = 3; i < m_tapFrame.size(); i += 4) {
        m_tapFrame[i] = 255; // the screen shows no alpha (see captureBackBuffer())
    }
    m_frameTap(m_tapFrame, graphics::Extent{rect.width, rect.height});
}

void RenderEngine::requestCapture(std::uint64_t frameIndex, std::string path) {
    m_captureFrame = frameIndex;
    m_capturePath = std::move(path);
    m_capture.reset();
}

std::expected<CapturedFrame, Error> RenderEngine::captureBackBuffer(const std::string& path) const {
    const auto width = static_cast<std::size_t>(m_frameSize.width);
    const auto height = static_cast<std::size_t>(m_frameSize.height);
    const std::size_t rowBytes = width * 4;
    std::vector<std::uint8_t> bottomUp(rowBytes * height);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_frameSize.width, m_frameSize.height, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());
    if (glGetError() != GL_NO_ERROR) {
        return std::unexpected(Error{ErrorCode::PlatformFailure, "glReadPixels could not read the frame"});
    }
    // OpenGL's rows run bottom up; images run top down.
    std::vector<std::uint8_t> rgba(bottomUp.size());
    for (std::size_t y = 0; y < height; ++y) {
        std::memcpy(&rgba[y * rowBytes], &bottomUp[(height - 1 - y) * rowBytes], rowBytes);
    }
    // The screen shows no alpha: blending leaves partial alpha in the frame buffer (a fading atomic), which an image
    // viewer would otherwise composite over its own background.
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
        rgba[i] = 255;
    }

    // librw's PNG writer reports a failure only through its own error state, so remove any old file first and check
    // that a new one exists afterwards.
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    rw::Image* image = rw::Image::create(m_frameSize.width, m_frameSize.height, 32);
    image->allocate();
    std::memcpy(image->pixels, rgba.data(), rgba.size());
    rw::writePNG(image, path.c_str());
    image->destroy();
    std::error_code error;
    if (!std::filesystem::exists(path, error) || std::filesystem::file_size(path, error) == 0) {
        return std::unexpected(Error{ErrorCode::Io, "could not write the screenshot to " + path});
    }
    return CapturedFrame{m_frameSize, graphics::summarizeFrame(rgba, m_clearColour), path};
}

void RenderEngine::shutDown() noexcept {
    removeGlCounters(); // a later start loads OpenGL's functions afresh
    destroyCamera();
    stopCharacterSkin();
    if (m_textureLod) {
        m_textureLod = false;
        stopTextureLod();
    }
    // The reverse of start(): librw refuses each step unless the one before it has run. With the GL3 device, the stop
    // step destroys the window and the context and the close step releases the GL3 device's reference to SDL's video.
    if (m_librwStarted) {
        m_librwStarted = false;
        // The rule: every librw raster and texture, and every texture dictionary but the one librw makes for itself,
        // was destroyed before this point (the camera's above, the owners' in their own destructors). librw's stop
        // destroys the textures and dictionaries still listed, so an owner that destroys its own after the stop frees
        // it twice, and a raster destructor that calls OpenGL after it runs on a context that is gone; a raster that
        // never had a texture is left to leak and to sit in librw's stage caches. Cheap to check, ruinous to break.
        CONEY_ASSERT(rw::Raster::numAllocated == 0 && rw::Texture::numAllocated == 0 &&
                     rw::TexDictionary::numAllocated <= 1);
        // While the OpenGL context is still there to bind in, put librw's caches of it back to where a fresh process
        // has them (the NULL backend has no context). librw skips a call whose argument equals what it cached, and
        // names start again in the next context, so a cached name can match a different object there.
        // docs/research/graphics-restart.md lists what is cleared and what needs nothing.
        if (m_sdlWindow != nullptr && !m_glStubbed) {
            // The framebuffer bound: the window's own (0).
            rw::gl3::bindFramebuffer(0);
            // Fog off, as at start-up.
            rw::SetRenderState(rw::FOGENABLE, 0);
        }
        rw::Engine::stop();
        // The stop destroyed every shader, Coney's (stopTextureLod above) and librw's own. Two librw globals still
        // name one. The "shader in use" is never cleared, because Shader::destroy does not, and the next renderer's
        // first Shader::create reads through it to put that program back: a read of freed memory, which crashed with
        // a segmentation fault now and then when the freed block's page had gone (issue #757). The 2D shader a
        // program may override (Coney sets none) would draw with a freed shader. The rest of librw's globals are
        // cleared by its own term step, started afresh by its init step, or harmless; the start puts the texture
        // units right (bindWhiteTextureOnEveryUnit). The whole list: docs/research/graphics-restart.md.
        rw::gl3::currentShader = nullptr;
        rw::gl3::im2dOverrideShader = nullptr;
        rw::Engine::close();
        rw::Engine::term();
    }
    if (m_glStubbed) {
        m_glStubbed = false;
        glad_glDeleteTextures = savedDeleteTextures;
    }
    // The GPU backend's swapchain, device and window, while SDL's video still runs.
    if (m_gpu) {
        m_gpu.reset();
        m_sdlWindow = nullptr;
    }
    if (m_sdlStarted) {
        m_sdlStarted = false;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SDL_Quit();
    }
}

} // namespace coney::platform
