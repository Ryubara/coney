// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/reference_sprites.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <utility>

#include "core/assert.h"

namespace coney::graphics {

namespace {

// The icon ids of docs/references/radar-icons.md: those the scripts pass to HUDSetRadarItemTexture and those the
// radar code sets itself.
constexpr std::array<std::uint32_t, 21> kRadarIconIds{22,  27,  28,  29,  30,  31,  32,  33,  34,  69, 350,
                                                      351, 352, 353, 355, 356, 359, 360, 361, 362, 365};

// The traced sprites of docs/references/particles.md (its Sheet and Rect columns): the sheet records 0 `part_page0`,
// 1 `part_page1`, 4 `part_tv`, 7 `part_fire`, 8 `lighting`, and the unnamed 9, 27 and 409 by their name hashes
// (docs/research/particles.md#sprite-words).
constexpr std::array<ReferenceSprite, 58> kParticleSprites{{
    {"blo_splat", "part_page1", 5},
    {"blood_drop", "part_page1", 52},
    {"blood_spray", "part_page1", 52},
    {"bloosh", "part_page1", 46},
    {"coplights_glow", "lighting", 2},
    {"coplights_lens_flare", "lighting", 2},
    {"hud_radar_dot", "part_page0", 69},
    {"part_copcar_lights", "lighting", 2},
    {"part_explosion", "part_page1", 6},
    {"part_fire", "part_fire", 0},
    {"part_fire_large", "part_fire", 0},
    {"part_fire_large_ns", "part_fire", 0},
    {"part_fire_ns", "part_fire", 0},
    {"part_fire_plume", "0x39640d4c", 0},
    {"part_fire_tiki", "part_fire", 0},
    {"part_firebarrel", "part_fire", 0},
    {"part_firebarrel_ns", "part_fire", 0},
    {"part_firetruck_lights", "lighting", 2},
    {"part_gun_flash", "lighting", 2},
    {"part_narrowflame", "0x5e5e43c6", 0},
    {"part_s_fire", "part_fire", 0},
    {"part_s_subway_sparks", "part_page1", 54},
    {"part_squareflame_lrg", "0x5e5e43c6", 0},
    {"part_squareflame_med", "0x5e5e43c6", 0},
    {"part_squareflame_sml", "0x5e5e43c6", 0},
    {"part_torch_flame", "part_fire", 0},
    {"part_torch_flame_ns", "part_fire", 0},
    {"part_train_splat", "part_page1", 6},
    {"part_tv", "part_tv", 0},
    {"part_urine_stain2", "part_page1", 6},
    {"spark", "part_page1", 41},
    {"sub_anim_spark", "part_page1", 50},
    {"sub_barlamp_glow", "lighting", 3},
    {"sub_blight_glow", "lighting", 3},
    {"sub_blood_gout", "part_page1", 2},
    {"sub_blood_spray", "part_page1", 6},
    {"sub_car_rubble", "part_page1", 24},
    {"sub_car_sparks", "part_page1", 45},
    {"sub_embers", "part_page1", 20},
    {"sub_explode", "part_page1", 17},
    {"sub_fade_flame", "part_fire", 0},
    {"sub_fire_smoke", "part_page1", 42},
    {"sub_flame_reflect", "part_fire", 0},
    {"sub_flaming_debris", "part_fire", 0},
    {"sub_glint", "part_page1", 41},
    {"sub_muzzle_flash", "part_page1", 35},
    {"sub_objective_glow", "lighting", 3},
    {"sub_polar_bugs", "part_page1", 19},
    {"sub_powerup_glow", "part_page1", 29},
    {"sub_shack_puff", "part_page1", 42},
    {"sub_shack_puff_aligned", "part_page1", 42},
    {"sub_splash", "part_page1", 51},
    {"sub_thrown_dust_puff", "part_page1", 42},
    {"sub_train_splat", "part_page1", 2},
    {"sub_train_splat_mist", "part_page1", 6},
    {"subway_lensflare", "0xcf4c87d3", 0},
    {"subway_spark", "part_page1", 54},
    {"urine_spray", "part_page1", 54},
}};

// How far a corner may sit past a texel edge and still count as on it: float noise in a coordinate that is meant to
// be whole.
constexpr float kEdgeSlack = 1e-3F;

// The whole texels [first, last) a span of texture coordinates covers on an axis of `size` texels, clamped to it and
// at least one texel long.
std::pair<int, int> texelSpan(float from, float to, int size) {
    int first = static_cast<int>(std::floor((from * static_cast<float>(size)) + kEdgeSlack));
    int last = static_cast<int>(std::ceil((to * static_cast<float>(size)) - kEdgeSlack));
    first = std::clamp(first, 0, size - 1);
    last = std::clamp(last, first + 1, size);
    return {first, last};
}

} // namespace

TexelBox rectTexels(const UvRect& rect, int textureWidth, int textureHeight) {
    CONEY_ASSERT(textureWidth > 0 && textureHeight > 0);
    const auto [x0, x1] = texelSpan(rect.u0, rect.u1, textureWidth);
    const auto [y0, y1] = texelSpan(rect.v0, rect.v1, textureHeight);
    return TexelBox{x0, y0, x1 - x0, y1 - y0};
}

ImageSize fitWithin(int width, int height, int limit, bool enlarge) {
    CONEY_ASSERT(width > 0 && height > 0 && limit > 0);
    const int longer = std::max(width, height);
    if (longer <= limit && !enlarge) {
        return ImageSize{width, height};
    }
    // The longer side becomes the limit; the shorter one keeps the proportion, rounded to nearest.
    const auto scaled = [longer, limit](int side) {
        return std::max(1, static_cast<int>(((static_cast<long long>(side) * limit) + (longer / 2)) / longer));
    };
    return ImageSize{scaled(width), scaled(height)};
}

std::vector<std::uint8_t> cropRgba(std::span<const std::uint8_t> rgba, int width, int height, const TexelBox& box) {
    CONEY_ASSERT(rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    CONEY_ASSERT(box.x >= 0 && box.y >= 0 && box.width > 0 && box.height > 0 && box.x + box.width <= width &&
                 box.y + box.height <= height);
    const auto rowBytes = static_cast<std::size_t>(box.width) * 4;
    std::vector<std::uint8_t> out(rowBytes * static_cast<std::size_t>(box.height));
    for (int y = 0; y < box.height; ++y) {
        const std::size_t from = ((static_cast<std::size_t>(box.y + y) * static_cast<std::size_t>(width)) +
                                  static_cast<std::size_t>(box.x)) *
                                 4;
        std::copy_n(rgba.begin() + static_cast<std::ptrdiff_t>(from), rowBytes,
                    out.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * rowBytes));
    }
    return out;
}

std::vector<std::uint8_t> resampleRgba(std::span<const std::uint8_t> rgba, int width, int height, int outWidth,
                                       int outHeight) {
    CONEY_ASSERT(width > 0 && height > 0 && outWidth > 0 && outHeight > 0);
    CONEY_ASSERT(rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    // Lengths are counted in units of 1/outWidth of a source pixel across and 1/outHeight down, so every overlap is
    // a whole number: output pixel x covers [x * width, (x + 1) * width), source pixel s covers
    // [s * outWidth, (s + 1) * outWidth).
    const auto overlap = [](long long a0, long long a1, long long b0, long long b1) {
        return std::max(0LL, std::min(a1, b1) - std::max(a0, b0));
    };
    const auto area = static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * 4);
    for (int y = 0; y < outHeight; ++y) {
        const long long top = static_cast<long long>(y) * height;
        const long long bottom = top + height;
        for (int x = 0; x < outWidth; ++x) {
            const long long left = static_cast<long long>(x) * width;
            const long long right = left + width;
            unsigned long long alpha = 0;
            unsigned long long red = 0;
            unsigned long long green = 0;
            unsigned long long blue = 0;
            for (auto sy = static_cast<int>(top / outHeight);
                 sy < height && sy * static_cast<long long>(outHeight) < bottom; ++sy) {
                const long long down = overlap(top, bottom, static_cast<long long>(sy) * outHeight,
                                               static_cast<long long>(sy + 1) * outHeight);
                for (auto sx = static_cast<int>(left / outWidth);
                     sx < width && sx * static_cast<long long>(outWidth) < right; ++sx) {
                    const long long across = overlap(left, right, static_cast<long long>(sx) * outWidth,
                                                     static_cast<long long>(sx + 1) * outWidth);
                    const auto weight = static_cast<unsigned long long>(down * across);
                    const std::size_t at = ((static_cast<std::size_t>(sy) * static_cast<std::size_t>(width)) +
                                            static_cast<std::size_t>(sx)) *
                                           4;
                    const unsigned long long a = rgba[at + 3] * weight;
                    red += rgba[at] * a;
                    green += rgba[at + 1] * a;
                    blue += rgba[at + 2] * a;
                    alpha += a;
                }
            }
            const std::size_t to =
                ((static_cast<std::size_t>(y) * static_cast<std::size_t>(outWidth)) + static_cast<std::size_t>(x)) * 4;
            if (alpha != 0) {
                // Undo the alpha weighting, rounding to nearest; the weights of one output pixel add up to `area`.
                out[to] = static_cast<std::uint8_t>((red + (alpha / 2)) / alpha);
                out[to + 1] = static_cast<std::uint8_t>((green + (alpha / 2)) / alpha);
                out[to + 2] = static_cast<std::uint8_t>((blue + (alpha / 2)) / alpha);
                out[to + 3] = static_cast<std::uint8_t>((alpha + (area / 2)) / area);
            }
        }
    }
    return out;
}

std::span<const std::uint32_t> radarIconIds() { return kRadarIconIds; }

std::span<const ReferenceSprite> particleSprites() { return kParticleSprites; }

std::string radarIconFileName(std::uint32_t id) { return std::format("icon-{}.png", id); }

} // namespace coney::graphics
