// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/render_engine.h"

#include <algorithm>
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
#include "platform/movie_screen.h"
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

    // OpenGL: start SDL's video first and check for a display. librw's GL3 device does start SDL's video itself, but
    // it ignores its own failures and goes on to use a display list it never made; holding our own reference also
    // keeps SDL alive until librw has destroyed its window.
    if (openGl) {
        // Before librw makes and shows the window, so a run nobody is playing never takes the keyboard focus.
        setWindowActivation(desc.activate && !desc.hidden);
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
    // Plugins are registered between init and open, as librw requires: the streamed world's (platform/world_atomic.h).
    attachWorldPlugins();
    // Each converted texture's GS level parameters, for drawing at the original's mip levels (platform/texture_lod.h).
    attachTextureLodPlugin();
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
    // librw's GL3 device always shows the window it makes; a tool that draws only into offscreen buffers hides it
    // again (the OpenGL context stays usable).
    if (desc.hidden) {
        SDL_HideWindow(static_cast<SDL_Window*>(engine->m_sdlWindow));
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(static_cast<SDL_Window*>(engine->m_sdlWindow), &width, &height);
    engine->m_frameSize = graphics::Extent{width, height};
    engine->createCamera();
    // Mip levels by distance, as the GS picks them; without it OpenGL picks them by screen size.
    engine->m_textureLod = startTextureLod();
    return engine;
}

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
    drawTexturedQuads(texture, quads, false);
}

void RenderEngine::drawWrappedQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads) {
    drawTexturedQuads(texture, quads, true);
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
                                     bool wrap) {
    CONEY_ASSERT(m_inFrame);
    if (m_camera == nullptr || quads.empty()) {
        return; // NULL backend: nothing to draw
    }
    rw::Raster* raster = bindTexture(texture, wrap);
    // Logical pixels to window pixels, then the shared 2D drawing.
    std::vector<graphics::LogicalQuad> mapped(quads.begin(), quads.end());
    for (graphics::LogicalQuad& quad : mapped) {
        const graphics::LogicalRect rect =
            graphics::logicalToWindow(graphics::LogicalRect{quad.x, quad.y, quad.width, quad.height}, m_viewport);
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
    // The modes' 2D layers for this frame first, taken out first so that a layer may add the next frame's; then the
    // debug menus over them.
    for (const std::function<void(RenderEngine&)>& overlay : std::exchange(m_frameOverlays, {})) {
        overlay(*this);
    }
    // The video output's softening covers everything the game draws, but not the debug menus.
    if (m_lineBlend && m_camera != nullptr) {
        blendLines();
    }
    if (m_presentOverlay) {
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
        m_camera->showRaster(m_vsync ? rw::Raster::FLIPWAITVSYNCH : 0);

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
    const graphics::ScreenRect viewport = m_viewport;
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
    const graphics::ScreenRect viewport = m_viewport;
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
    const float shift = 1.0F / graphics::kLogicalHeight;
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
    destroyCamera();
    if (m_textureLod) {
        m_textureLod = false;
        stopTextureLod();
    }
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
