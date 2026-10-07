// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

namespace coney::graphics {

/// A colour with 8 bits per channel, alpha 255 for opaque.
struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    friend bool operator==(const Rgba&, const Rgba&) = default;
};

/// Opaque black: the colour the start-up screens clear to.
inline constexpr Rgba kBlack{0, 0, 0, 255};
/// Opaque white: a sprite drawn in it shows its texture unchanged.
inline constexpr Rgba kWhite{255, 255, 255, 255};

/// A rectangle of texture coordinates: (u0, v0) the top-left corner, (u1, v1) the bottom-right, 0 to 1 across the
/// texture. The sprite sheets store theirs this way (docs/research/gui.md#particle-page).
struct UvRect {
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;

    friend bool operator==(const UvRect&, const UvRect&) = default;
};

/// A texture a RenderDevice can draw. The platform layer implements it over librw's textures; game code only holds it
/// and passes it back to the device, so it never sees a graphics API.
class Texture {
  public:
    virtual ~Texture() = default;
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture&&) = delete;

    /// Width of the texture in texels.
    [[nodiscard]] virtual int width() const = 0;
    /// Height of the texture in texels.
    [[nodiscard]] virtual int height() const = 0;
};

/// One textured rectangle of a 2D pass, in the logical screen's pixels (graphics/screen.h).
struct LogicalQuad {
    float x = 0.0F;      ///< Left edge, logical pixels from the left of the screen.
    float y = 0.0F;      ///< Top edge, logical pixels from the top.
    float width = 0.0F;  ///< Logical pixels.
    float height = 0.0F; ///< Logical pixels.
    UvRect uv;           ///< The part of the texture stretched over it.
    Rgba colour;         ///< Multiplies the texture; its alpha blends the quad over what is below.
};

/// One corner of a 2D triangle, in the logical screen's pixels, with its texture coordinate and colour.
struct LogicalVertex {
    float x = 0.0F; ///< Logical pixels from the left of the screen.
    float y = 0.0F; ///< Logical pixels from the top.
    float u = 0.0F; ///< Texture coordinate across, 0 to 1 over the texture.
    float v = 0.0F; ///< Texture coordinate down.
    Rgba colour;    ///< Multiplies the texture; its alpha blends the triangle over what is below.
};

/// How a drawTriangles() call samples and tests: the texture's addressing, and the GS alpha test that drops a fragment
/// whose alpha (the texel's times the vertex's, 0 to 1) is below `alphaRef` (0: no test, every fragment kept).
struct TriangleStates {
    bool wrap = false;     ///< Repeat the texture beyond 0-1; otherwise its edge texels repeat.
    float alphaRef = 0.0F; ///< Fragments with alpha below this are not drawn.
};

/// The part of the renderer a game mode drives each frame: start a frame cleared to a colour, draw 2D quads, then
/// show it.
///
/// It stands for the original's render device, which each game mode's update calls to begin the frame and, at the
/// end, to present it (docs/research/boot.md#one-frame, steps 6 and 10); in Coney a mode's render() makes these calls
/// (gamemodes/game_mode.h), never its update(). The platform layer implements it with librw

/// (src/platform/render_engine.h); the headless renderer implements every call as a no-op, so a mode never needs to
/// know whether anything is on screen.
class RenderDevice {
  public:
    virtual ~RenderDevice() = default;
    RenderDevice() = default;
    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;
    RenderDevice(RenderDevice&&) = delete;
    RenderDevice& operator=(RenderDevice&&) = delete;

    /// Starts a frame with the logical screen cleared to `clear` (and any border around it in the window to black,
    /// see graphics::fitLogicalScreen()). Drawing happens between this and present().
    virtual void beginFrame(Rgba clear) = 0;

    /// Draws `quads` with `texture` (null: flat colour) in the 2D pass's states: depth test and depth write off, no
    /// culling, blended by source alpha over what is already drawn (docs/research/graphics.md#2d-drawing). The quads
    /// are drawn in the order given. Only between beginFrame() and present(); `texture` must stay alive until the call
    /// returns.
    virtual void drawQuads(const Texture* texture, std::span<const LogicalQuad> quads) = 0;

    /// Draws `vertices` as a list of triangles (three corners each; a remainder is ignored) with `texture` (null: flat
    /// colour) in the same 2D states as drawQuads(), the colour and texture coordinates blended across each triangle:
    /// the original's immediate-mode 2D shapes (the radar's disc, docs/research/hud.md#the-radar-on-screen), sampled
    /// and tested as `states` says.
    virtual void drawTriangles(const Texture* texture, std::span<const LogicalVertex> vertices,
                               const TriangleStates& states) = 0;

    /// Ends the frame and shows it. With a display and vsync on this waits for the vertical blank; the headless
    /// renderer returns at once. The game's speed does not depend on it: the main loop steps the game by real time
    /// (core/frame_clock.h), not by presents.

    virtual void present() = 0;
};

} // namespace coney::graphics
