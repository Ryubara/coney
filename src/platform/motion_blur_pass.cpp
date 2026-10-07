// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/motion_blur_pass.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <rw.h>

namespace coney::platform {

MotionBlurPass::~MotionBlurPass() { release(); }

void MotionBlurPass::release() {
    if (m_texture != nullptr) {
        m_texture->destroy(); // destroys its raster too
    } else if (m_raster != nullptr) {
        m_raster->destroy();
    }
    m_texture = nullptr;
    m_raster = nullptr;
    m_kept = false;
}

void MotionBlurPass::apply(RenderEngine& engine, effects::MotionBlur::Colour colour) {
    rw::Camera* camera = engine.camera();
    if (!engine.drawsPixels() || camera == nullptr) {
        return;
    }
    // No blur: nothing to lay over, and the next blur starts from a fresh frame.
    if (colour.a == 0) {
        m_kept = false;
        return;
    }
    const graphics::ScreenRect viewport = engine.logicalViewport();
    if (viewport.width <= 0 || viewport.height <= 0) {
        return;
    }
    // The kept frame's texture, the logical screen's size; made again when the window is resized.
    if (m_raster == nullptr || m_width != viewport.width || m_height != viewport.height) {
        release();
        m_raster = rw::Raster::create(viewport.width, viewport.height, 32,
                                      static_cast<rw::int32>(rw::Raster::TEXTURE) | rw::Raster::C8888);
        if (m_raster == nullptr) {
            return;
        }
        m_texture = rw::Texture::create(m_raster);
        m_texture->setFilter(rw::Texture::LINEAR);
        m_width = viewport.width;
        m_height = viewport.height;
    }

    // 1. The kept frame over the new one: the screen's quad in the blur's colour and strength. librw's 2D drawing
    // samples the copy with v = 0 at the screen's top (measured with RenderEngine's copies, which are made the same
    // way).
    if (m_kept) {
        rw::SetRenderState(rw::ZTESTENABLE, 0);
        rw::SetRenderState(rw::ZWRITEENABLE, 0);
        rw::SetRenderState(rw::VERTEXALPHA, 1);
        rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
        rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
        rw::SetRenderState(rw::TEXTUREFILTER, rw::Texture::LINEAR);
        rw::SetRenderState(rw::TEXTUREADDRESS, rw::Texture::CLAMP);
        rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
        rw::SetRenderStatePtr(rw::TEXTURERASTER, m_raster);
        const auto left = static_cast<float>(viewport.x);
        const auto top = static_cast<float>(viewport.y);
        const auto right = static_cast<float>(viewport.x + viewport.width);
        const auto bottom = static_cast<float>(viewport.y + viewport.height);
        struct Corner {
            float x, y, u, v;
        };
        const std::array<Corner, 4> corners{
            {{left, top, 0, 0}, {right, top, 1, 0}, {right, bottom, 1, 1}, {left, bottom, 0, 1}}};
        const float nearZ = rw::im2d::GetNearZ();
        const float recipZ = 1.0F / camera->nearPlane;
        std::array<rw::gl3::Im2DVertex, 4> vertices{};
        for (std::size_t i = 0; i < corners.size(); ++i) {
            rw::gl3::Im2DVertex& vertex = vertices[i];
            vertex.setScreenX(corners[i].x);
            vertex.setScreenY(corners[i].y);
            vertex.setScreenZ(nearZ);
            vertex.setRecipCameraZ(recipZ);
            vertex.setColor(colour.r, colour.g, colour.b, colour.a);
            vertex.setU(corners[i].u, recipZ);
            vertex.setV(corners[i].v, recipZ);
        }
        std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
        rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, vertices.data(), static_cast<rw::int32>(vertices.size()),
                                         indices.data(), static_cast<rw::int32>(indices.size()));
        rw::SetRenderState(rw::ZTESTENABLE, 1);
        rw::SetRenderState(rw::ZWRITEENABLE, 1);
        rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
    }

    // 2. Keep the blended frame: the logical screen's pixels copied into the texture (GL counts rows from the bottom).
    const int windowHeight = engine.frameSize().height;
    auto* native = PLUGINOFFSET(rw::gl3::Gl3Raster, m_raster, rw::gl3::nativeRasterOffset);
    rw::gl3::bindTexture(native->texid);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport.x, windowHeight - (viewport.y + viewport.height),
                        viewport.width, viewport.height);
    m_kept = true;
}

} // namespace coney::platform
