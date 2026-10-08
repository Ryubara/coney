// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"

namespace coney::effects {

/// `SetPositionOfWater(pos, rot, width, length, colour, waveHeight, waveSpeed)`'s settings
/// (docs/references/bindings/world.md#setpositionofwater).
struct WaterSettings {
    anim::Vec3 position;                  ///< The grid's corner, z the water's height.
    anim::Quat rotation{0, 0, 0, 1};      ///< Normalised; all zeros is the identity.
    float width = 0.0F;                   ///< Metres along the grid's x (its columns).
    float length = 0.0F;                  ///< Metres along the grid's y (its rows).
    std::array<std::uint8_t, 3> colour{}; ///< RGB, RenderWare's 0-255.
    float waveHeight = 0.0F;              ///< Metres each column rises and falls by.
    float waveSpeed = 0.0F;               ///< Multiplies the phase for the waves and the texture's scroll.
};

/// One vertex of the water's grid, in the grid's own unit square: x and y from 0 to 1, z the wave's height in metres.
struct WaterVertex {
    anim::Vec3 position;
    std::array<std::uint8_t, 4> colour{}; ///< RGBA, 0-255.
    float u = 0.0F;
    float v = 0.0F;
};

/// The level's single water surface (`WaterEffect`, `Graphics/WaterEffect.cpp`; graphics.md#code-water): a grid of 3
/// × 9 vertices (2 columns, 8 rows, as `Water_Set` always makes it) on the unit square, scaled to the width and length
/// (the wave height in metres, unscaled), turned and placed by the settings, textured `water_tex` with 4 repeats
/// across and 16 along, and drawn by its vertex colour times the texture, unlit. Every frame the world pass advances a
/// phase by 0.16; when it has moved more than 0.3 since the last update (every second frame), with `t = phase ×
/// waveSpeed`: the texture scrolls to `u0 = fmod(t × 0.3, 4)`, column `i` (all but the last) rises to `sin(i + t) ×
/// waveHeight` with alpha `225 + 5 sin(i + t)` over the colour, its texture coordinates are `u0 + 4 i / 2` across and
/// `16 j / 8` along, and the last column copies the first, so the surface tiles.
///
/// Coney's choices: the phase advances once a fixed step (the original's frame); the grid exists from the first
/// `SetPositionOfWater`, without the original's streaming of its texture file.
///
/// @orig 0x00191230 WaterEffect_Init (WaterEffect.cpp)
/// @orig 0x00191dd8 WaterEffect_Update (WaterEffect.cpp)
class Water {
  public:
    static constexpr int kColumns = 3; ///< Vertices across.
    static constexpr int kRows = 9;    ///< Vertices along.
    static constexpr float kRepeatsAcross = 4.0F;
    static constexpr float kRepeatsAlong = 16.0F;
    static constexpr float kPhasePerFrame = 0.16F;
    static constexpr float kUpdateEvery = 0.3F; ///< Phase between two updates (`0x0050cd84`).
    static constexpr float kScrollRate = 0.3F;  ///< `0x0050cd90`.
    static constexpr float kBaseAlpha = 225.0F; ///< `0x0050cd8c`.
    static constexpr float kAlphaSwing = 5.0F;  ///< `0x0050cd88`.
    /// The texture's name; its WAD file is named by the decimal CRC-32 of it (a disc check: `725908093`).
    static constexpr std::string_view kTexture = "water_tex";

    Water();

    /// `SetPositionOfWater`: places, sizes and colours the surface and sets its waves; the grid is rebuilt at once.
    void set(const WaterSettings& settings);
    /// Whether a script has placed the water.
    [[nodiscard]] bool placed() const { return m_settings.has_value(); }
    /// The settings, when placed.
    [[nodiscard]] const std::optional<WaterSettings>& settings() const { return m_settings; }

    /// One frame of the world pass: the phase advances, and the grid is updated when it has moved far enough.
    void step();

    /// The grid's vertices, row by row (`j × kColumns + i`).
    [[nodiscard]] const std::vector<WaterVertex>& vertices() const { return m_vertices; }
    /// Two triangles a cell, as vertex indices, wound as the grid's rows and columns run.
    [[nodiscard]] static const std::vector<std::uint16_t>& indices();

    /// Where the unit grid's point `p` lies in the world: scaled by width and length, turned, then placed.
    [[nodiscard]] anim::Vec3 toWorld(anim::Vec3 p) const;

  private:
    // Rewrites the heights, colours and texture coordinates for the current phase.
    void update();

    std::optional<WaterSettings> m_settings;
    std::vector<WaterVertex> m_vertices;
    float m_phase = 0.0F;
    float m_lastUpdate = 0.0F;
};

} // namespace coney::effects
