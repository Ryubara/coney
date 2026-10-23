// SPDX-License-Identifier: GPL-3.0-or-later

// The renderer can be stopped and started again in one process, as a change of display, of video settings or of mode
// would do, and the new one meets no state of the old one: no librw global points at an object the stop destroyed, and
// no cache describes an OpenGL context that is gone (issue #757; docs/research/graphics-restart.md).
// The window is hidden and never takes the keyboard focus. Without a display and OpenGL 3.3 the test skips, as
// coney.frame_copies_upright does.

#include <array>
#include <cstdint>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "graphics/render_device.h"
#include "platform/render_engine.h"

namespace {

using coney::graphics::LogicalQuad;
using coney::graphics::Rgba;
using coney::platform::RenderEngine;

constexpr int kWidth = 640;
constexpr int kHeight = 448;

// One pixel of the frame being drawn, `y` counted from the window's top.
Rgba readPixel(int x, int y) {
    std::array<unsigned char, 4> pixel{};
    glReadPixels(x, kHeight - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    return Rgba{pixel[0], pixel[1], pixel[2], pixel[3]};
}

// The name of the 2D texture bound on texture unit `unit` in OpenGL itself, not in librw's cache of it. Leaves unit 0
// active, where librw believes it is.
GLint boundOnUnit(int unit) {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    GLint name = -1;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &name);
    glActiveTexture(GL_TEXTURE0);
    return name;
}

// Makes framebuffer names in the current context until one is `wanted`, and returns it; 0 when the driver never gets
// there (its names do not run on from 1, or `wanted` is out of reach). The names made are left to the context.
GLuint makeFramebufferNamed(GLuint wanted) {
    constexpr int kMostNames = 4096;
    for (int i = 0; i < kMostNames; ++i) {
        GLuint name = 0;
        glGenFramebuffers(1, &name);
        if (name == wanted) {
            return name;
        }
    }
    return 0;
}

// Draws red over blue, and returns whether the top row reads red and the bottom row blue: a frame that renders right.
bool rendersRedOverBlue(RenderEngine& engine) {
    const std::array<LogicalQuad, 2> halves{{
        {0.0F, 0.0F, kWidth, kHeight / 2.0F, {}, Rgba{255, 0, 0, 255}},
        {0.0F, kHeight / 2.0F, kWidth, kHeight / 2.0F, {}, Rgba{0, 0, 255, 255}},
    }};
    engine.beginFrame(Rgba{0, 0, 0, 255});
    engine.drawWindowRects(halves);
    const Rgba top = readPixel(kWidth / 2, 4);
    const Rgba bottom = readPixel(kWidth / 2, kHeight - 5);
    engine.present();
    return top.r > 2 * top.b && bottom.b > 2 * bottom.r;
}

} // namespace

TEST_CASE("a renderer started again after one stopped meets none of its state", "[render][window]") {
    coney::platform::WindowDesc desc;
    desc.title = "Coney renderer restart check";
    desc.width = kWidth;
    desc.height = kHeight;
    desc.hidden = true;
    desc.logicalFrame = true;

    constexpr int kStarts = 4;
    GLuint staleFramebuffer = 0; // the framebuffer name librw has cached as bound when the last renderer stopped
    for (int start = 0; start < kStarts; ++start) {
        INFO("start " << start);
        auto started = RenderEngine::start(coney::platform::RenderBackend::OpenGl, desc);
        if (!started) {
            // Without a display or OpenGL the first start cannot happen and the test skips; a start after one that
            // worked is a renderer that cannot restart, which is the failure this test is for.
            if (start == 0) {
                SKIP("no OpenGL window here: " << started.error().message);
            }
            FAIL("the renderer could not start again: " << started.error().message);
        }
        RenderEngine& engine = **started;

        // What the last renderer left behind must not show. This runs before the first frame, whose camera would
        // bind framebuffer 0 and white on unit 0 and so hide a stale cache.
        CHECK(rw::gl3::currentShader == nullptr);
        CHECK(rw::gl3::im2dOverrideShader == nullptr);
        CHECK(rw::GetRenderState(rw::FOGENABLE) == 0);
        CHECK(rw::GetRenderStatePtr(rw::TEXTURERASTER) == nullptr);
        // librw leaves unit 0 active, and the checks below read the others by making each active in turn.
        GLint active = -1;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        CHECK(active == static_cast<GLint>(GL_TEXTURE0));
        // librw believes its white texture is bound on all eight texture units, and the new context has to agree:
        // librw skips a bind of the name it thinks is bound, and the new context's white texture gets the old one's
        // name, so units 1 to 7 stayed on texture 0 (nothing).
        const GLint white = boundOnUnit(0);
        CHECK(white != 0);
        for (int unit = 1; unit < 8; ++unit) {
            INFO("texture unit " << unit);
            CHECK(boundOnUnit(unit) == white);
        }
        // A framebuffer name librw cached as bound in the old context must not stop the first bind of the same name in
        // this one: make names here until the old one comes up (the driver need not hand them out in the old order).
        if (staleFramebuffer != 0) {
            const GLuint same = makeFramebufferNamed(staleFramebuffer);
            if (same == 0) {
                WARN("the driver never made framebuffer name " << staleFramebuffer << " again: not checked");
            } else {
                rw::gl3::bindFramebuffer(same);
                GLint bound = -1;
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
                CHECK(bound == static_cast<GLint>(same));
                rw::gl3::bindFramebuffer(0);
            }
        }

        CHECK(rendersRedOverBlue(engine));
        CHECK(rw::gl3::currentShader != nullptr); // drawing put a shader in use; the checks below mean something
        // The first draw uploaded librw's uniforms to its registry, where new shaders read them: the fog colour a
        // renderer starts with is white, whatever the last one set.
        const rw::gl3::Uniform& fogColor = rw::gl3::uniformRegistry.uniforms[rw::gl3::findUniform("u_fogColor")];
        const auto* fogRgba = static_cast<const float*>(fogColor.data);
        CHECK((fogRgba[0] == 1.0F && fogRgba[1] == 1.0F && fogRgba[2] == 1.0F && fogRgba[3] == 1.0F));
        rw::SetRenderState(rw::FOGCOLOR, 0xFF0000FFU); // red, opaque
        CHECK(rendersRedOverBlue(engine));             // a frame later the registry holds it
        CHECK(fogRgba[0] == 1.0F);
        CHECK(fogRgba[1] == 0.0F);

        // Leave state in librw as a program might as the renderer stops: a framebuffer bound through it, the 2D shader
        // overridden with a shader the stop will destroy, fog on, and a texture held in a stage (destroyed before the
        // stop, as the rule is, which empties the stage and puts white back on its unit).
        GLuint last = 0;
        glGenFramebuffers(1, &last);
        rw::gl3::bindFramebuffer(last);
        staleFramebuffer = last;
        rw::gl3::im2dOverrideShader = rw::gl3::currentShader;
        rw::SetRenderState(rw::FOGENABLE, 1);
        rw::Raster* raster =
            rw::Raster::create(16, 16, 32, static_cast<rw::int32>(rw::Raster::TEXTURE) | rw::Raster::C8888);
        REQUIRE(raster != nullptr);
        rw::Texture* texture = rw::Texture::create(raster);
        rw::gl3::setTexture(1, texture);
        texture->destroy(); // destroys its raster too

        started->reset();

        // Every raster, texture and dictionary was destroyed before the stop, as the rule is; the stop asserts it.
        CHECK(rw::Raster::numAllocated == 0);
        CHECK(rw::Texture::numAllocated == 0);
        CHECK(rw::TexDictionary::numAllocated == 0);
        // The stop destroyed every shader; nothing may still name one.
        CHECK(rw::gl3::currentShader == nullptr);
        CHECK(rw::gl3::im2dOverrideShader == nullptr);
    }
}
