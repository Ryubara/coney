// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A render device for tests: it draws nothing and records every call a mode or a pass makes, in order.

#include <span>
#include <string>
#include <vector>

#include "graphics/render_device.h"

namespace coney::test {

/// One drawQuads() call as the device saw it.
struct RecordedDraw {
    const graphics::Texture* texture = nullptr;
    std::vector<graphics::LogicalQuad> quads;
};

/// One drawTriangles() call as the device saw it.
struct RecordedTriangles {
    const graphics::Texture* texture = nullptr;
    std::vector<graphics::LogicalVertex> vertices;
    graphics::TriangleStates states;
};

/// Records "begin", "draw", "triangles" and "present" in `calls`, the clear colours in `clears` and each draw in
/// `draws`.
class RecordingDevice final : public graphics::RenderDevice {
  public:
    void beginFrame(graphics::Rgba clear) override {
        calls.emplace_back("begin");
        clears.push_back(clear);
    }
    void drawQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads) override {
        calls.emplace_back("draw");
        draws.push_back(RecordedDraw{texture, {quads.begin(), quads.end()}});
    }
    void drawTriangles(const graphics::Texture* texture, std::span<const graphics::LogicalVertex> vertices,
                       const graphics::TriangleStates& states) override {
        calls.emplace_back("triangles");
        triangles.push_back(RecordedTriangles{texture, {vertices.begin(), vertices.end()}, states});
    }
    void present() override { calls.emplace_back("present"); }

    std::vector<std::string> calls;
    std::vector<graphics::Rgba> clears;
    std::vector<RecordedDraw> draws;
    std::vector<RecordedTriangles> triangles;
};

/// A texture of a given size that is nothing but its size.
class FakeTexture final : public graphics::Texture {
  public:
    FakeTexture(int width, int height) : m_width(width), m_height(height) {}
    [[nodiscard]] int width() const override { return m_width; }
    [[nodiscard]] int height() const override { return m_height; }

  private:
    int m_width;
    int m_height;
};

} // namespace coney::test
