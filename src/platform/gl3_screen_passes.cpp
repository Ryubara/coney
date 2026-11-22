// SPDX-License-Identifier: GPL-3.0-or-later
// The GL3 renderer's screen passes (platform/gl3_renderer.h): the blur pulse, the line blend and the motion blur, each
// drawn from a copy of the frame. A file of their own beside gl3_renderer.cpp, which draws the frame's other passes.
#include "platform/gl3_renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include <rw.h>

#include "platform/draw_calls.h"

namespace coney::platform {

namespace {

// Drops `raster` if there is one (as gl3_renderer.cpp's own).
void destroyRaster(rw::Raster*& raster) {
    if (raster != nullptr) {
        raster->destroy();
        raster = nullptr;
    }
}

} // namespace

void Gl3Renderer::drawScreenPass(const graphics::ScreenPass& pass, const Target& target) {
    // Each copies the frame, so it needs one: nothing with the NULL backend, even while recording.
    if (target.camera == nullptr) {
        return;
    }
    if (const auto* blur = std::get_if<graphics::BlurPulse>(&pass.effect); blur != nullptr) {
        drawBlurPulse(*blur, pass.area, target);
    } else if (const auto* lines = std::get_if<graphics::LineBlend>(&pass.effect); lines != nullptr) {
        drawLineBlend(*lines, pass.area, target);
    } else {
        drawMotionBlur(std::get<graphics::MotionBlurOverlay>(pass.effect), pass.area, target);
    }
}

void Gl3Renderer::drawBlurPulse(const graphics::BlurPulse& blur, const graphics::ScreenRect& viewport,
                                const Target& target) {
    // The whole picture the 3D view fills, as the original blurs its whole screen (in a wide window, past the logical
    // screen's 4:3 place).
    if (viewport.width < 2 || viewport.height < 2) {
        return;
    }
    draw::setRenderState(rw::TEXTUREFILTER, rw::Texture::LINEAR);
    draw::setRenderState(rw::TEXTUREADDRESS, rw::Texture::CLAMP);
    const auto left = static_cast<float>(viewport.x);
    const auto top = static_cast<float>(viewport.y);
    const int halfWidth = viewport.width / 2;
    const int halfHeight = viewport.height / 2;
    const graphics::Rgba white{255, 255, 255, 255};

    // 1. The screen at half size, drawn into its own top-left quarter (scratch: the last step covers all of it).
    // The original draws each step into its 512 x 256 raster through its effects camera; with one back buffer, Coney
    // draws into the frame and copies the result back (copyToRaster) before the next step reads it. The UVs span each
    // copy, without the original's half-texel offset (pixel + 0.5 over the size), which places a GS sample where a GL
    // texel centre already is (inferred). Both are stand-ins.
    if (!copyToRaster(m_blurRaster, viewport.x, viewport.y, viewport.width, viewport.height, target)) {
        return;
    }
    const graphics::LogicalQuad half{
        left, top, static_cast<float>(halfWidth), static_cast<float>(halfHeight), graphics::UvRect{}, white};
    drawWindowQuads(m_blurRaster, std::span<const graphics::LogicalQuad>(&half, 1), target, true);

    // 2. The passes: the half-size image onto itself with one edge moved in by half the offset, the low edges (u0, v0)
    // for a positive offset and the high edges for a negative one, the sign flipping each pass, resampled linearly.
    // The offsets are in UV units of the original's 512 x 256 texture, whose half-size image is 320 x 224 (inferred:
    // a 640 x 448 screen at half size), so they are scaled to the same share of the image here.
    const float stepU = blur.offsetU / 2.0F * (512.0F / 320.0F);
    const float stepV = blur.offsetV / 2.0F * (256.0F / 224.0F);
    bool lowEdge = true;
    for (int pass = 0; pass < blur.passes; ++pass) {
        // Stand-in: a copy of the half-size image for the pass to read, as in step 1.
        if (!copyToRaster(m_halfRaster, viewport.x, viewport.y, halfWidth, halfHeight, target)) {
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
        // Stand-in: UVs over the copy, without the half-texel offset (step 1).
        drawWindowQuads(m_halfRaster, std::span<const graphics::LogicalQuad>(&quad, 1), target, true);
        lowEdge = !lowEdge;
    }

    // 3. Stretched back over the whole screen, its edges inset by one texel, opaque.
    if (!copyToRaster(m_halfRaster, viewport.x, viewport.y, halfWidth, halfHeight, target)) {
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
    drawWindowQuads(m_halfRaster, std::span<const graphics::LogicalQuad>(&whole, 1), target, true);
    // 4. What each of the original's steps puts back after its quad: Z test and write on, blending by alpha. The 2D
    // states also turn culling off and clamp the texture, which the original leaves as they were (stand-in: Coney's
    // shared 2D states; each pass after this sets its own culling and addressing).
    draw::setRenderState(rw::ZTESTENABLE, 1);
    draw::setRenderState(rw::ZWRITEENABLE, 1);
    draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    draw::setRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
}

void Gl3Renderer::drawLineBlend(const graphics::LineBlend& blend, const graphics::ScreenRect& viewport,
                                const Target& target) {
    // The whole picture the 3D view fills: the video output softens everything it sends, so in a wide window the
    // view's sides too, not only the logical screen's 4:3 place (which would leave a visible edge 12.5 % in).
    if (viewport.width <= 0 || viewport.height <= 0) {
        return;
    }
    // 1. The frame as drawn, copied.
    if (!copyToRaster(m_lineRaster, viewport.x, viewport.y, viewport.width, viewport.height, target)) {
        return;
    }

    // 2. Laid over itself at half strength, shifted so that each window row shows the frame one original line (448 to
    // the screen's height) below it: the PS2's second read circuit starts one line down (DISPFB2's DBY 1) and the
    // two are mixed half and half (PMODE's ALP 0x80). Past the copy's bottom edge it is clamped, as the original's last
    // line has no line below it. Filtered linearly, so a window that is not a whole number of lines per original line
    // still blends exactly one original line.
    draw::setRenderState(rw::TEXTUREFILTER, rw::Texture::LINEAR);
    draw::setRenderState(rw::TEXTUREADDRESS, rw::Texture::CLAMP);
    const graphics::LogicalQuad quad{static_cast<float>(viewport.x),
                                     static_cast<float>(viewport.y),
                                     static_cast<float>(viewport.width),
                                     static_cast<float>(viewport.height),
                                     graphics::UvRect{0.0F, blend.lineShift, 1.0F, 1.0F + blend.lineShift},
                                     graphics::Rgba{255, 255, 255, 128}};
    drawWindowQuads(m_lineRaster, std::span<const graphics::LogicalQuad>(&quad, 1), target);
}

void Gl3Renderer::drawMotionBlur(const graphics::MotionBlurOverlay& blur, const graphics::ScreenRect& viewport,
                                 const Target& target) {
    KeptCopy* copy = keptCopy(blur.copy);
    if (copy == nullptr) {
        return;
    }
    // No blur: nothing to lay over, and the next blur starts from a fresh frame.
    if (blur.colour.a == 0) {
        copy->kept = false;
        return;
    }
    if (viewport.width <= 0 || viewport.height <= 0) {
        return;
    }
    // The kept frame's raster, the view's size; made again when the window is resized.
    if (copy->raster == nullptr || copy->width != viewport.width || copy->height != viewport.height) {
        destroyRaster(copy->raster);
        copy->kept = false;
        copy->raster = draw::createRaster(viewport.width, viewport.height, 32,
                                          static_cast<rw::int32>(rw::Raster::TEXTURE) | rw::Raster::C8888);
        if (copy->raster == nullptr) {
            return;
        }
        copy->width = viewport.width;
        copy->height = viewport.height;
    }

    // 1. The kept frame over the new one: the screen's quad in the blur's colour and strength. librw's 2D drawing
    // samples the copy with v = 0 at the screen's top (measured with the other screen passes' copies, which are made
    // the same way). Not the shared 2D states: fog is left as the 3D pass set it, and culling is put back after.
    if (copy->kept) {
        draw::setRenderState(rw::ZTESTENABLE, 0);
        draw::setRenderState(rw::ZWRITEENABLE, 0);
        draw::setRenderState(rw::VERTEXALPHA, 1);
        draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
        draw::setRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
        draw::setRenderState(rw::TEXTUREFILTER, rw::Texture::LINEAR);
        draw::setRenderState(rw::TEXTUREADDRESS, rw::Texture::CLAMP);
        draw::setRenderState(rw::CULLMODE, rw::CULLNONE);
        draw::setRenderStatePtr(rw::TEXTURERASTER, copy->raster);
        const auto left = static_cast<float>(viewport.x);
        const auto top = static_cast<float>(viewport.y);
        const auto right = static_cast<float>(viewport.x + viewport.width);
        const auto bottom = static_cast<float>(viewport.y + viewport.height);
        struct Corner {
            float x, y, u, v;
        };
        const std::array<Corner, 4> corners{
            {{left, top, 0, 0}, {right, top, 1, 0}, {right, bottom, 1, 1}, {left, bottom, 0, 1}}};
        const float nearZ = draw::im2dNearZ();
        const float recipZ = 1.0F / nearPlaneOf(target);
        std::array<rw::gl3::Im2DVertex, 4> vertices{};
        for (std::size_t i = 0; i < corners.size(); ++i) {
            rw::gl3::Im2DVertex& vertex = vertices[i];
            vertex.setScreenX(corners[i].x);
            vertex.setScreenY(corners[i].y);
            vertex.setScreenZ(nearZ);
            vertex.setRecipCameraZ(recipZ);
            vertex.setColor(blur.colour.r, blur.colour.g, blur.colour.b, blur.colour.a);
            vertex.setU(corners[i].u, recipZ);
            vertex.setV(corners[i].v, recipZ);
        }
        std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
        draw::im2dRenderIndexedPrimitive(rw::PRIMTYPETRILIST, vertices.data(), static_cast<rw::int32>(vertices.size()),
                                         indices.data(), static_cast<rw::int32>(indices.size()));
        draw::setRenderState(rw::ZTESTENABLE, 1);
        draw::setRenderState(rw::ZWRITEENABLE, 1);
        draw::setRenderState(rw::CULLMODE, rw::CULLBACK);
    }

    // 2. Keep the blended frame: the view's pixels copied into the raster (GL counts rows from the bottom).
    draw::copyFrameToRaster(copy->raster, viewport.x, viewport.y, viewport.width, viewport.height,
                            target.frameSize.height);
    copy->kept = true;
}

} // namespace coney::platform
