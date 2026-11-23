// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/test_card_mode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <span>
#include <utility>

#include "effects/blur_pulse.h"
#include "effects/motion_blur.h"
#include "graphics/frame_description.h"
#include "graphics/screen.h"
#include "platform/movie_screen.h"
#include "platform/pixel_texture.h"
#include "platform/render_engine.h"
#include "platform/test_card_scene.h"

namespace coney::platform {

namespace {

using graphics::LogicalQuad;
using graphics::LogicalVertex;
using graphics::Rgba;
using graphics::UvRect;

// The side of the cards' textures, in texels.
constexpr int kTextureSide = 16;

// A texture of `rgba` (kTextureSide square, rows top first) for the engine's renderer, sampled with `filter`: a librw
// raster for OpenGL (MovieTexture), pixels for the GPU renderer and the NULL backend (PixelTexture).
std::unique_ptr<graphics::Texture> makeTexture(const RenderEngine& engine, std::span<const std::uint8_t> rgba,
                                               graphics::TextureFilter filter) {
    if (engine.backend() == RenderBackend::OpenGl) {
        auto texture = std::make_unique<MovieTexture>(filter);
        texture->write(rgba, kTextureSide, kTextureSide);
        return texture;
    }
    auto texture = std::make_unique<PixelTexture>(filter);
    texture->write(rgba, kTextureSide, kTextureSide);
    return texture;
}

// A checkerboard of 4-texel squares in two colours, a red stripe down the left column and a blue one along the top row,
// so a texture drawn the wrong way round or shifted by a texel shows.
std::vector<std::uint8_t> checkerTexels() {
    std::vector<std::uint8_t> texels;
    texels.reserve(static_cast<std::size_t>(kTextureSide) * kTextureSide * 4);
    for (int y = 0; y < kTextureSide; ++y) {
        for (int x = 0; x < kTextureSide; ++x) {
            Rgba colour = ((x / 4) + (y / 4)) % 2 == 0 ? Rgba{240, 230, 200, 255} : Rgba{40, 90, 70, 255};
            if (x == 0) {
                colour = Rgba{220, 30, 30, 255};
            } else if (y == 0) {
                colour = Rgba{30, 60, 220, 255};
            }
            texels.insert(texels.end(), {colour.r, colour.g, colour.b, colour.a});
        }
    }
    return texels;
}

// White texels whose alpha rises from 0 at the left column to 255 at the right, in even steps.
std::vector<std::uint8_t> alphaRampTexels() {
    std::vector<std::uint8_t> texels;
    texels.reserve(static_cast<std::size_t>(kTextureSide) * kTextureSide * 4);
    for (int y = 0; y < kTextureSide; ++y) {
        for (int x = 0; x < kTextureSide; ++x) {
            const auto alpha = static_cast<std::uint8_t>(x * 255 / (kTextureSide - 1));
            texels.insert(texels.end(), {255, 255, 255, alpha});
        }
    }
    return texels;
}

// The six corners of two triangles over the logical rectangle (`x`, `y`, `w`, `h`), with texture coordinates 0 to
// `uv` across it and `colour` at every corner.
std::array<LogicalVertex, 6> rectangleTriangles(float x, float y, float w, float h, float uv, Rgba colour) {
    const LogicalVertex topLeft{x, y, 0.0F, 0.0F, colour};
    const LogicalVertex topRight{x + w, y, uv, 0.0F, colour};
    const LogicalVertex bottomRight{x + w, y + h, uv, uv, colour};
    const LogicalVertex bottomLeft{x, y + h, 0.0F, uv, colour};
    return {topLeft, topRight, bottomRight, topLeft, bottomRight, bottomLeft};
}

} // namespace

std::expected<std::unique_ptr<TestCardMode>, Error> TestCardMode::create(RenderEngine& engine, std::string_view name) {
    const auto found = std::ranges::find(kTestCardNames, name);
    if (found == kTestCardNames.end()) {
        return fail(ErrorCode::NotFound,
                    std::format("no test card \"{}\" (shapes, textures or triangles, or the 3D cards scene, effects, "
                                "blur-pulse, line-blend and motion-blur)",
                                name));
    }
    const auto card = static_cast<Card>(found - kTestCardNames.begin());
    return std::unique_ptr<TestCardMode>(new TestCardMode(engine, card));
}

// Makes the cards' textures once, so every frame draws the same ones.
TestCardMode::TestCardMode(RenderEngine& engine, Card card) : m_engine(engine), m_card(card) {
    // The textures, made once: a checkerboard filtered linearly, the same to the nearest texel, an alpha ramp.
    const std::vector<std::uint8_t> checker = checkerTexels();
    m_textures.push_back(makeTexture(engine, checker, graphics::TextureFilter::Linear));
    m_textures.push_back(makeTexture(engine, checker, graphics::TextureFilter::Nearest));
    m_textures.push_back(makeTexture(engine, alphaRampTexels(), graphics::TextureFilter::Linear));
    if (card != Card::Shapes && card != Card::Textures && card != Card::Triangles) {
        m_scene = std::make_unique<TestCardScene>(engine);
    }
}

TestCardMode::~TestCardMode() = default;

void TestCardMode::render(const RenderTime& /*time*/) {
    switch (m_card) {
    case Card::Shapes:
        drawShapes();
        break;
    case Card::Textures:
        drawTextures();
        break;
    case Card::Triangles:
        drawTriangles();
        break;
    case Card::Scene:
        m_scene->draw();
        break;
    case Card::Effects:
        m_scene->draw();
        drawEffects();
        break;
    case Card::BlurPulse:
    case Card::LineBlend:
    case Card::MotionBlur:
        drawScreenPassCard();
        return; // presented there
    }
    m_engine.present();
}

// A screen pass card: the scene card with its screen pass over the whole view, as the play mode draws them after its
// 3D frame (the motion blur's from PlayLevelEffects::drawOverlay(), the blur pulse's from drawScreenEffects(), the
// line blend's at present()).
void TestCardMode::drawScreenPassCard() {
    m_scene->draw();
    const bool lineBlend = m_engine.lineBlend();
    switch (m_card) {
    case Card::BlurPulse: {
        // The pulse at its full level, in its look's passes and offsets (`CfgScrFx`'s look 5).
        const effects::BlurPulse::Look look;
        m_engine.blurScreen(look.passes, look.offsetU, look.offsetV);
        break;
    }
    case Card::LineBlend:
        m_engine.setLineBlend(true);
        break;
    case Card::MotionBlur:
        m_motionBlur.apply(m_engine, effects::MotionBlur::Colour{255, 200, 160, 128});
        break;
    default:
        break;
    }
    m_engine.present();
    m_engine.setLineBlend(lineBlend);
}

// The shapes card: opaque bars, blended quads, one quad too faint for the alpha test, a wash and window rectangles.
void TestCardMode::drawShapes() {
    // A slate logical screen, black round it (beginFrame()'s fill quad).
    m_engine.beginFrame(Rgba{32, 36, 52, 255});
    // Eight opaque bars across the top, in the colours of a television's bars.
    constexpr std::array<Rgba, 8> bars{{{235, 235, 235, 255},
                                        {235, 235, 16, 255},
                                        {16, 235, 235, 255},
                                        {16, 235, 16, 255},
                                        {235, 16, 235, 255},
                                        {235, 16, 16, 255},
                                        {16, 16, 235, 255},
                                        {16, 16, 16, 255}}};
    std::array<LogicalQuad, 8> barQuads{};
    for (std::size_t i = 0; i < bars.size(); ++i) {
        barQuads[i] = LogicalQuad{40.0F + (static_cast<float>(i) * 70.0F), 32.0F, 70.0F, 140.0F, UvRect{}, bars[i]};
    }
    m_engine.drawQuads(nullptr, barQuads);
    // Three quads blended over the bars and each other at decreasing alpha.
    const std::array<LogicalQuad, 3> blended{{{80.0F, 120.0F, 200.0F, 160.0F, UvRect{}, Rgba{255, 64, 0, 160}},
                                              {200.0F, 160.0F, 200.0F, 160.0F, UvRect{}, Rgba{0, 128, 255, 96}},
                                              {320.0F, 200.0F, 200.0F, 160.0F, UvRect{}, Rgba{255, 255, 0, 48}}}};
    m_engine.drawQuads(nullptr, blended);
    // A quad too faint for the 2D quads' alpha test (6 of 255, under GL3's 10): neither renderer draws it.
    const LogicalQuad faint{40.0F, 380.0F, 560.0F, 40.0F, UvRect{}, Rgba{255, 255, 255, 6}};
    m_engine.drawQuads(nullptr, std::span<const LogicalQuad>(&faint, 1));
    // A wash over the whole view, as the screen fades draw theirs (wider than the logical screen in a wide window).
    const LogicalQuad wash{
        0.0F, 0.0F, graphics::kLogicalWidth, graphics::kLogicalHeight, UvRect{}, Rgba{64, 0, 128, 40}};
    m_engine.drawViewQuads(nullptr, std::span<const LogicalQuad>(&wash, 1));
    // A square of 8 window pixels in each corner of the window, given in window pixels.
    const graphics::Extent size = m_engine.frameSize();
    const auto right = static_cast<float>(size.width - 8);
    const auto bottom = static_cast<float>(size.height - 8);
    const std::array<LogicalQuad, 4> corners{{{0.0F, 0.0F, 8.0F, 8.0F, UvRect{}, Rgba{255, 0, 0, 255}},
                                              {right, 0.0F, 8.0F, 8.0F, UvRect{}, Rgba{0, 255, 0, 255}},
                                              {0.0F, bottom, 8.0F, 8.0F, UvRect{}, Rgba{0, 0, 255, 255}},
                                              {right, bottom, 8.0F, 8.0F, UvRect{}, Rgba{255, 255, 255, 255}}}};
    m_engine.drawWindowRects(corners);
}

// The textures card: the checkerboard at its size, enlarged both ways, repeated, tinted, and the alpha ramp.
void TestCardMode::drawTextures() {
    m_engine.beginFrame(Rgba{20, 20, 20, 255});
    const graphics::Texture* linear = m_textures[0].get();
    const graphics::Texture* nearest = m_textures[1].get();
    const graphics::Texture* ramp = m_textures[2].get();
    // At its own size: a texel a logical pixel.
    const LogicalQuad actual{24.0F, 24.0F, 16.0F, 16.0F, UvRect{}, graphics::kWhite};
    m_engine.drawQuads(linear, std::span<const LogicalQuad>(&actual, 1));
    // Enlarged eight times, filtered linearly, then to the nearest texel.
    const LogicalQuad enlarged{64.0F, 24.0F, 128.0F, 128.0F, UvRect{}, graphics::kWhite};
    m_engine.drawQuads(linear, std::span<const LogicalQuad>(&enlarged, 1));
    const LogicalQuad sharp{216.0F, 24.0F, 128.0F, 128.0F, UvRect{}, graphics::kWhite};
    m_engine.drawQuads(nearest, std::span<const LogicalQuad>(&sharp, 1));
    // Repeated three times each way.
    const LogicalQuad repeated{368.0F, 24.0F, 240.0F, 128.0F, UvRect{0.0F, 0.0F, 3.0F, 3.0F}, graphics::kWhite};
    m_engine.drawWrappedQuads(linear, std::span<const LogicalQuad>(&repeated, 1));
    // Its middle quarter, tinted orange and half transparent.
    const LogicalQuad tinted{
        64.0F, 184.0F, 200.0F, 120.0F, UvRect{0.25F, 0.25F, 0.75F, 0.75F}, Rgba{255, 160, 64, 128}};
    m_engine.drawQuads(nearest, std::span<const LogicalQuad>(&tinted, 1));
    // The alpha ramp over three coloured bars: each bar shows through less from left to right.
    const std::array<LogicalQuad, 3> bars{{{300.0F, 184.0F, 300.0F, 40.0F, UvRect{}, Rgba{200, 40, 40, 255}},
                                           {300.0F, 224.0F, 300.0F, 40.0F, UvRect{}, Rgba{40, 200, 40, 255}},
                                           {300.0F, 264.0F, 300.0F, 40.0F, UvRect{}, Rgba{40, 40, 200, 255}}}};
    m_engine.drawQuads(nullptr, bars);
    const LogicalQuad overBars{300.0F, 184.0F, 300.0F, 120.0F, UvRect{}, Rgba{255, 255, 255, 255}};
    m_engine.drawQuads(ramp, std::span<const LogicalQuad>(&overBars, 1));
}

// The triangles card: a fan with a colour at each corner, the ramp alpha tested at a half and at 0, and the
// checkerboard repeated on triangles.
void TestCardMode::drawTriangles() {
    m_engine.beginFrame(Rgba{16, 24, 16, 255});
    // A fan of 24 triangles round a white centre, each outer corner its own colour, every fragment kept.
    constexpr int kSegments = 24;
    constexpr float kCentreX = 160.0F;
    constexpr float kCentreY = 200.0F;
    constexpr float kRadius = 128.0F;
    std::vector<LogicalVertex> fan;
    fan.reserve(static_cast<std::size_t>(kSegments) * 3);
    const auto rim = [&](int i) {
        const float angle = static_cast<float>(i) * 2.0F * std::numbers::pi_v<float> / kSegments;
        const auto shade = static_cast<std::uint8_t>((i * 255) / kSegments);
        return LogicalVertex{kCentreX + (kRadius * std::cos(angle)), kCentreY + (kRadius * std::sin(angle)), 0.0F, 0.0F,
                             Rgba{shade, static_cast<std::uint8_t>(255 - shade), 128, 255}};
    };
    for (int i = 0; i < kSegments; ++i) {
        fan.push_back(LogicalVertex{kCentreX, kCentreY, 0.0F, 0.0F, graphics::kWhite});
        fan.push_back(rim(i));
        fan.push_back(rim(i + 1));
    }
    m_engine.drawTriangles(nullptr, fan, graphics::TriangleStates{false, 0.0F});
    // The alpha ramp on triangles: at a half, its left half dropped; at 0, all of it blended.
    const graphics::Texture* ramp = m_textures[2].get();
    const auto tested = rectangleTriangles(320.0F, 64.0F, 256.0F, 96.0F, 1.0F, Rgba{255, 220, 120, 255});
    m_engine.drawTriangles(ramp, tested, graphics::TriangleStates{false, 0.5F});
    const auto kept = rectangleTriangles(320.0F, 176.0F, 256.0F, 96.0F, 1.0F, Rgba{255, 220, 120, 255});
    m_engine.drawTriangles(ramp, kept, graphics::TriangleStates{false, 0.0F});
    // The checkerboard on triangles, repeated twice each way.
    const auto repeated = rectangleTriangles(320.0F, 288.0F, 256.0F, 128.0F, 2.0F, graphics::kWhite);
    m_engine.drawTriangles(m_textures[0].get(), repeated, graphics::TriangleStates{true, 0.0F});
}

// The effects card's effect pass, after the scene card through its camera (the screen's left is +x): each draw sets
// the states the game's own effects set (platform/particle_renderer.cpp, water_renderer.cpp, line_list.cpp).
void TestCardMode::drawEffects() {
    using graphics::EffectBlend;
    using graphics::EffectDraw;
    using graphics::EffectVertex;
    using Point = std::array<float, 3>;
    // Two triangles over the corners as the camera sees them, texture coordinates 0 to 1 across, `colour` at each.
    const auto quad = [](Point topLeft, Point topRight, Point bottomLeft, Point bottomRight, Rgba colour) {
        const auto corner = [colour](Point p, float u, float v) {
            return EffectVertex{p[0], p[1], p[2], colour, u, v};
        };
        return std::array<EffectVertex, 6>{corner(topLeft, 0.0F, 0.0F),    corner(bottomLeft, 0.0F, 1.0F),
                                           corner(topRight, 1.0F, 0.0F),   corner(topRight, 1.0F, 0.0F),
                                           corner(bottomLeft, 0.0F, 1.0F), corner(bottomRight, 1.0F, 1.0F)};
    };
    // A particle's states: depth tested, not written, unfogged, unculled, the vertex alpha used, then put back.
    const auto particle = [](EffectBlend blend) {
        EffectDraw draw;
        draw.states.depthTest = true;
        draw.states.depthWrite = false;
        draw.states.fog = false;
        draw.states.vertexAlpha = true;
        draw.states.cull = graphics::CullFaces::None;
        draw.states.blend = blend;
        draw.restore = graphics::EffectRestore{.fog = true, .blend = true, .depthWrite = true, .cull = true};
        return draw;
    };

    // A translucent sheen on the floor, which sets no depth or fog state (as the water does): drawn in the scene's.
    EffectDraw sheen;
    sheen.states.cull = graphics::CullFaces::None;
    sheen.states.vertexAlpha = true;
    sheen.states.blend = EffectBlend::Alpha;
    sheen.restore.cull = true;
    m_engine.drawEffect(sheen,
                        quad({5.0F, 0.02F, 45.0F}, {-5.0F, 0.02F, 45.0F}, {5.0F, 0.02F, 18.0F}, {-5.0F, 0.02F, 18.0F},
                             Rgba{120, 170, 255, 140}),
                        {}, nullptr);
    // A grey sprite blended by its vertex alpha, in front of the sign.
    m_engine.drawEffect(
        particle(EffectBlend::Alpha),
        quad({3.5F, 3.0F, 8.0F}, {1.5F, 3.0F, 8.0F}, {3.5F, 1.0F, 8.0F}, {1.5F, 1.0F, 8.0F}, Rgba{200, 200, 200, 120}),
        {}, nullptr);
    // An orange glow added, its left edge behind the crate.
    m_engine.drawEffect(particle(EffectBlend::Additive),
                        quad({-1.0F, 4.0F, 12.0F}, {-3.0F, 4.0F, 12.0F}, {-1.0F, 1.0F, 12.0F}, {-3.0F, 1.0F, 12.0F},
                             Rgba{255, 160, 40, 160}),
                        {}, nullptr);
    // The alpha ramp as a sprite, its vertex alpha off: its texture's alpha blends it.
    EffectDraw ramp;
    ramp.states.vertexAlpha = false;
    ramp.states.fog = false;
    ramp.states.filter = graphics::TextureFilter::Linear;
    ramp.states.address = graphics::TextureAddress::Clamp;
    m_engine.drawEffect(ramp,
                        quad({-0.5F, 1.4F, 5.0F}, {-2.5F, 1.4F, 5.0F}, {-0.5F, 0.4F, 5.0F}, {-2.5F, 0.4F, 5.0F},
                             Rgba{255, 255, 255, 255}),
                        {}, m_textures[2].get());
    // A line list round the crate, as the debug tools' lines set their states.
    EffectDraw lines;
    lines.shape = graphics::EffectShape::Lines;
    lines.states.depthTest = true;
    lines.states.depthWrite = false;
    lines.states.vertexAlpha = false;
    lines.restore.depthWrite = true;
    const Rgba red{230, 40, 40, 255};
    const std::array<Point, 4> box{
        {{1.2F, 2.7F, 6.9F}, {-1.7F, 2.7F, 6.9F}, {-1.7F, 0.05F, 6.9F}, {1.2F, 0.05F, 6.9F}}};
    std::array<EffectVertex, 8> edges{};
    for (std::size_t i = 0; i < box.size(); ++i) {
        const Point& from = box[i];
        const Point& to = box[(i + 1) % box.size()];
        edges[i * 2] = EffectVertex{from[0], from[1], from[2], red, 0.0F, 0.0F};
        edges[(i * 2) + 1] = EffectVertex{to[0], to[1], to[2], red, 0.0F, 0.0F};
    }
    m_engine.drawEffect(lines, edges, {}, nullptr);
}

} // namespace coney::platform
