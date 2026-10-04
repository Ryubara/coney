// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/render_engine.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <rw.h>

#include "core/assert.h"

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

} // namespace

RenderEngine::RenderEngine(RenderBackend backend, const WindowDesc& desc)
    : m_backend(backend), m_title(desc.title), m_frameSize{desc.width, desc.height} {}

RenderEngine::~RenderEngine() { shutDown(); }

std::expected<std::unique_ptr<RenderEngine>, Error> RenderEngine::start(RenderBackend backend, const WindowDesc& desc) {
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<RenderEngine> engine(new RenderEngine(backend, desc));
    const bool openGl = backend == RenderBackend::OpenGl;

    // OpenGL: start SDL's video first and check for a display. librw's GL3 device does start SDL's video itself, but
    // it ignores its own failures and goes on to use a display list it never made; holding our own reference also
    // keeps SDL alive until librw has destroyed its window.
    if (openGl) {
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

    // librw's three start-up steps. The default memory functions (malloc and free) are fine until Coney has an
    // allocator of its own.
    if (!rw::Engine::init()) {
        return startFailure("librw Engine::init", false);
    }
    rw::EngineOpenParams params{};
    params.window = reinterpret_cast<SDL_Window**>(&engine->m_sdlWindow);
    params.fullscreen = 0;
    params.width = desc.width;
    params.height = desc.height;
    params.windowtitle = engine->m_title.c_str();
    // Engine::open copies the device for this platform (always GL3 in Coney's build) into the engine and opens it.
    // For the NULL backend, put librw's NULL device in that slot for the duration of the copy, then put GL3 back: the
    // engine then runs on the NULL device until it closes, and SDL's video is never started.
    bool opened = false;
    if (openGl) {
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
    if (!openGl) {
        return engine;
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
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(static_cast<SDL_Window*>(engine->m_sdlWindow), &width, &height);
    engine->m_frameSize = graphics::Extent{width, height};
    engine->createCamera();
    return engine;
}

std::optional<Window> RenderEngine::window() const {
    if (m_sdlWindow == nullptr) {
        return std::nullopt;
    }
    return Window(m_sdlWindow);
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
    CONEY_ASSERT(!m_inFrame);
    m_inFrame = true;
    m_clearColour = clear;
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

void RenderEngine::present() {
    CONEY_ASSERT(m_inFrame);
    m_inFrame = false;
    const bool captureThisFrame = m_captureFrame.has_value() && *m_captureFrame == m_presented;
    if (m_camera != nullptr) {
        m_camera->endUpdate();
        // Read the frame back before it is shown: after the buffer swap the back buffer's contents are undefined.
        if (captureThisFrame) {
            m_capture = captureBackBuffer(m_capturePath);
        }
        m_camera->showRaster(rw::Raster::FLIPWAITVSYNCH);
    } else if (captureThisFrame) {
        m_capture = std::unexpected(Error{ErrorCode::PlatformFailure, "the headless renderer draws no frames"});
    }
    ++m_presented;
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
    destroyCamera();
    // The reverse of start(): librw refuses each step unless the one before it has run. With the GL3 device, the stop
    // step destroys the window and the context and the close step releases the GL3 device's reference to SDL's video.
    if (m_librwStarted) {
        m_librwStarted = false;
        rw::Engine::stop();
        rw::Engine::close();
        rw::Engine::term();
    }
    if (m_glStubbed) {
        m_glStubbed = false;
        glad_glDeleteTextures = savedDeleteTextures;
    }
    if (m_sdlStarted) {
        m_sdlStarted = false;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SDL_Quit();
    }
}

} // namespace coney::platform
