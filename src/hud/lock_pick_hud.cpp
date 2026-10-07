// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/lock_pick_hud.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "graphics/overlay_camera.h"

namespace coney::hud {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// The pins' GUI places and the shapes' centres per player, in the default video mode (LockPickDial_Draw's override).
constexpr std::array<GuiPoint, kPlayers> kPinPlaces{GuiPoint{0.09F, 0.59F}, GuiPoint{0.9F, 0.59F}};
constexpr std::array<float, kPlayers> kShapeCentreX{-0.565F, 0.546F};
constexpr float kShapeCentreY = -0.07F;
// The lock face's texture is turned a quarter turn.
constexpr float kFaceTurn = kPi / 2.0F;
// A rim vertex outside a wedge: colour 0, alpha 0.
constexpr graphics::Rgba kClear{0, 0, 0, 0};

// The overlay-space size of one logical pixel at distance 1, where the shapes' centre is.
graphics::LogicalPoint pixelInOverlay() {
    static const graphics::LogicalPoint unit = graphics::OverlayCamera{}.unprojectSize({1.0F, 1.0F}, 1.0F);
    return unit;
}

// Rim vertex `j` of a 32-segment circle: angle π − j × 2π / 32 (π, the first, straight up), placed `radius` pixels
// from `centre` along (sin a, cos a) with y down on screen.
float rimAngle(int j) { return kPi - (static_cast<float>(j) * 2.0F * kPi / static_cast<float>(kLockSegments)); }

graphics::OverlayPoint rimPoint(graphics::OverlayPoint centre, float radius, float a) {
    const graphics::LogicalPoint unit = pixelInOverlay();
    return graphics::OverlayPoint{centre.x + (radius * std::sin(a) * unit.x),
                                  centre.y - (radius * std::cos(a) * unit.y), centre.z};
}

} // namespace

void LockPickHud::show(bool on, int difficulty) {
    m_shown = on;
    m_difficulty = std::clamp(difficulty, 0, 2);
    if (on) {
        m_angles.fill(kPi);
        m_current = 0;
    }
}

void LockPickHud::setPins(const std::array<float, kLockPinCount>& angles, int current) {
    m_angles = angles;
    m_current = current;
}

GuiPoint LockPickHud::pinPlace(std::size_t player) { return kPinPlaces.at(std::min(player, kPlayers - 1)); }

graphics::OverlayPoint LockPickHud::shapeCentre(std::size_t player) {
    return graphics::OverlayPoint{kShapeCentreX.at(std::min(player, kPlayers - 1)), kShapeCentreY, 1.0F};
}

int LockPickHud::wedgeVertices(float percent) {
    return static_cast<int>(std::floor(static_cast<float>(kLockSegments) * percent / 100.0F / 2.0F));
}

void LockPickHud::renderWedge(graphics::SpriteBatch& batch, graphics::OverlayPoint centre, float percent,
                              graphics::Rgba colour) {
    if (percent <= 0.0F) {
        return;
    }
    // Rim vertex j takes the colour within k of the middle on either side (the dial's mirror flag); the centre always
    // has it, so the segments next to the wedge fade to clear and the rest fade from the centre outwards.
    const int k = wedgeVertices(percent);
    const auto rim = [&](int j) {
        const bool inside = j <= k || kLockSegments - j <= k;
        return graphics::OverlayVertex{rimPoint(centre, kLockBandRadius, rimAngle(j)), 0.0F, 0.0F,
                                       inside ? colour : kClear};
    };
    const graphics::OverlayVertex middle{centre, 0.0F, 0.0F, colour};
    for (int j = 0; j < kLockSegments; ++j) {
        batch.addTriangle(middle, rim(j), rim(j + 1));
    }
}

void LockPickHud::renderFace(graphics::SpriteBatch& batch, graphics::OverlayPoint centre) {
    const graphics::SpriteSheet& sheet = batch.sheet();
    if (kLockFaceRect >= sheet.page.rects.size()) {
        return;
    }
    // The rectangle's centre and half-size give the texture's circle; each rim vertex shows the point at its angle
    // turned by a quarter.
    const graphics::UvRect rect = sheet.page.rect(kLockFaceRect);
    const float cu = (rect.u0 + rect.u1) / 2.0F;
    const float cv = (rect.v0 + rect.v1) / 2.0F;
    const float ru = (rect.u1 - rect.u0) / 2.0F;
    const float rv = (rect.v1 - rect.v0) / 2.0F;
    const auto rim = [&](int j) {
        const float a = rimAngle(j);
        return graphics::OverlayVertex{rimPoint(centre, kLockFaceRadius, a), cu + (ru * std::sin(a + kFaceTurn)),
                                       cv + (rv * std::cos(a + kFaceTurn)), kLockPinColour};
    };
    const graphics::OverlayVertex middle{centre, cu, cv, kLockPinColour};
    for (int j = 0; j < kLockSegments; ++j) {
        batch.addTriangle(middle, rim(j), rim(j + 1));
    }
}

void LockPickHud::render(const HudCanvas& canvas, std::size_t player) const {
    if (!m_shown) {
        return;
    }
    // 1. The pins, largest first, each turned by its angle about the one centre; none once every pin is done.
    if (graphics::SpriteBatch* sheet = canvas.minigames; sheet != nullptr &&
                                                         m_current < static_cast<int>(kLockPinCount) &&
                                                         kLockPinRect < sheet->sheet().page.rects.size()) {
        const GuiPoint place = pinPlace(player);
        for (std::size_t i = 0; i < kLockPinCount; ++i) {
            const float size = kLockPinSize * (1.0F - (kLockPinShrink * static_cast<float>(i)));
            graphics::Sprite pin;
            pin.position = graphics::OverlayCamera::guiToOverlay(place.x, place.y);
            pin.width = size;
            pin.height = size;
            pin.uv = sheet->sheet().page.rect(kLockPinRect);
            pin.colour = kLockPinColour;
            sheet->addSprite(pin, m_angles.at(i));
        }
    }
    // 2. The shapes, after every sprite: band 1 (good), band 2 (perfect) over it, then the lock face over both.
    const graphics::OverlayPoint centre = shapeCentre(player);
    const auto difficulty = static_cast<std::size_t>(m_difficulty);
    if (canvas.shapes != nullptr) {
        renderWedge(*canvas.shapes, centre, kLockGoodPercent.at(difficulty), kLockGoodColour);
        renderWedge(*canvas.shapes, centre, kLockPerfectPercent.at(difficulty), kLockPerfectColour);
    }
    if (canvas.shapeFace != nullptr) {
        renderFace(*canvas.shapeFace, centre);
    }
}

} // namespace coney::hud
