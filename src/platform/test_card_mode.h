// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"

namespace coney::platform {

class RenderEngine;

/// The test cards `--test-card` draws, by name.
inline constexpr std::array<std::string_view, 3> kTestCardNames{"shapes", "textures", "triangles"};

/// Coney's own 2D test cards (`--test-card NAME`, docs/guides/running-viewers.md#test-cards): fixed pictures of the
/// 2D drawing every renderer must draw alike, made of Coney's own shapes and textures generated in code (no game data),
/// so CI's software pictures draw them on both renderers and compare them (docs/guides/testing.md#software-pictures).
/// Each card is one frame, drawn the same every frame:
///
/// - `shapes`: flat quads, opaque and blended by their alpha, one too faint for the 2D alpha test, a wash over the
///   whole view and rectangles in window pixels;
/// - `textures`: a texture at its own size and enlarged, filtered linearly and to the nearest texel, repeated, tinted,
///   and one with an alpha gradient over coloured bars;
/// - `triangles`: a fan of triangles with a colour at each corner, and textured triangles alpha tested at 0 (every
///   fragment) and at a half, clamped and repeated.
///
/// A debug tool, in the debug tools' build only; Coney's own, with no counterpart in the original.
class TestCardMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range of ids.
    static constexpr std::uint32_t kId = 0x108;

    /// The card `name` drawn through `engine`, which must outlive the mode; ErrorCode::NotFound naming the cards when
    /// there is no card of that name.
    [[nodiscard]] static std::expected<std::unique_ptr<TestCardMode>, Error> create(RenderEngine& engine,
                                                                                    std::string_view name);

    ~TestCardMode() override;
    TestCardMode(const TestCardMode&) = delete;
    TestCardMode& operator=(const TestCardMode&) = delete;
    TestCardMode(TestCardMode&&) = delete;
    TestCardMode& operator=(TestCardMode&&) = delete;

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    // Nothing to simulate: the card stays until the window closes or the frame limit.
    ModeResult update(GameModeStack& /*stack*/, const FrameTime& /*frame*/) override { return ModeResult::Stay; }

    // Draws the card and presents it.
    void render(const RenderTime& time) override;

  private:
    /// Which card.
    enum class Card : std::uint8_t { Shapes, Textures, Triangles };

    TestCardMode(RenderEngine& engine, Card card);

    // The shapes card: flat and blended quads, one too faint for the alpha test, and window rectangles.
    void drawShapes();
    // The textures card: one checkerboard drawn filtered, to the nearest texel, repeated and tinted.
    void drawTextures();
    // The triangles card: coloured triangles and two alpha tests on triangles.
    void drawTriangles();

    RenderEngine& m_engine;
    Card m_card;
    // The cards' textures, made for the engine's renderer (a librw raster for OpenGL, pixels for the GPU renderer).
    std::vector<std::unique_ptr<graphics::Texture>> m_textures;
};

} // namespace coney::platform
