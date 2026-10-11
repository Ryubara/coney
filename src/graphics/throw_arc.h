// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"

// The throw-aim arc: the strip drawn along the path a held object will fly while the player aims a throw with L1
// (docs/research/graphics.md#code-textures, docs/research/objects.md#throws).

namespace coney::graphics {

/// The arc's texture, a texture dictionary in the WAD named by the decimal CRC-32 of this name. **Coney's reading**:
/// the page gives the name and that it loads once (`0x005971b0`); the file's naming is the water's
/// (docs/research/graphics.md#code-water), inferred to hold for every name `ResourceMgr_GetTextures` is given.
inline constexpr std::string_view kThrowArcTexture = "aimthrow_tex";
/// The strip's width, metres.
inline constexpr float kThrowArcWidth = 0.16F;
/// The strip's colour (`0xRRGGBBAA` as bytes) while the arc meets nothing it targets: **Coney's stand-in** (the page
/// says one colour for each case, not which); a flat light grey, as players remember it.
inline constexpr std::array<std::uint8_t, 4> kThrowArcColour{170, 170, 170, 160};
/// The strip's colour while the arc meets a target (`hit`: a human target or an object with `THROWNWEAPONTARGET`,
/// what `HuIsAimingAt` reads): **Coney's stand-in**, red.
inline constexpr std::array<std::uint8_t, 4> kThrowArcHitColour{220, 30, 30, 160};
/// The points over which the strip fades in from the hand (alpha 0 at the first, full from this one on): **Coney's
/// stand-in** for the fade the page names but does not size.
inline constexpr std::size_t kThrowArcFadePoints = 3;

/// One vertex of the strip, game axes (z up).
struct ThrowArcVertex {
    anim::Vec3 position;
    float u = 0.0F; ///< 0 on the left edge, 1 on the right.
    float v = 0.0F; ///< 0 at the hand, 1 at the arc's last point.
    std::array<std::uint8_t, 4> rgba{};
};

/// The strip along the arc's `points` (the aim trace's kept points up to where it ended, the hand first): a pair of
/// vertices per point, left then right, kThrowArcWidth apart across the arc's horizontal direction, so the strip lies
/// flat; coloured kThrowArcHitColour when `hit`, else kThrowArcColour, faded in over the first kThrowArcFadePoints.
/// Empty for fewer than two points. **Coney's reading**: "flat" as lying level across the arc (the page gives the
/// width, not the strip's facing).
/// @orig 0x0018fa68 ThrowArc_BuildStrip (unknown)
[[nodiscard]] std::vector<ThrowArcVertex> throwArcStrip(std::span<const anim::Vec3> points, bool hit);

/// The arc's points `alpha` of the way from the step before (`previous`) to the newest (`newest`), for a render between
/// steps: each point lerped when both arcs have as many points, else `newest` as it is. Exactly `newest` at alpha 1.
[[nodiscard]] std::vector<anim::Vec3> blendThrowArc(std::span<const anim::Vec3> previous,
                                                    std::span<const anim::Vec3> newest, float alpha);

} // namespace coney::graphics
