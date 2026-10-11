// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/lock_pick_hud.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "graphics/overlay_camera.h"

namespace coney::hud {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// One player's centre for each mode `LockPickDial_Init` / `LockPickDial_ApplyVideoMode` writes (x, y at distance 1).
struct ModeCentres {
    std::array<float, kPlayers> x;
    float y;
};
constexpr ModeCentres kInterlacedCentres{{-0.56F, 0.545F}, -0.075F};
constexpr ModeCentres kInterlacedWideCentres{{-0.74F, 0.725F}, -0.075F};
// Progressive 4:3 rewrites nothing: the static table entry (0x0050d670 + player x 0x20, +0x10) stays.
constexpr ModeCentres kProgressiveCentres{{-0.531F, 0.519F}, -0.075F};
constexpr ModeCentres kProgressiveWideCentres{{-0.62F, 0.62F}, -0.075F};
// The interlaced 4:3 mode's shapes, which `LockPickDial_Draw` moves off the centre every update.
constexpr ModeCentres kInterlacedShapeCentres{{-0.565F, 0.546F}, -0.07F};

// The radius and size factors `LockPickDial_Draw` writes every update: wedges (x, y), face (x, y), pins (w, h).
struct ModeFactors {
    float wedgeX, wedgeY, faceX, faceY, pinW, pinH;
};
constexpr ModeFactors kInterlacedFactors{1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
constexpr ModeFactors kInterlacedWideFactors{0.8F, 1.0F, 0.9F, 1.0F, 1.0F, 1.0F};
constexpr ModeFactors kProgressiveFactors{0.75F, 1.1F, 0.75F, 1.0F, 0.6F, 0.6F};
constexpr ModeFactors kProgressiveWideFactors{0.5F, 0.95F, 0.5F, 0.85F, 0.6F, 0.6F};

// The lock face's texture is turned a quarter turn.
constexpr float kFaceTurn = kPi / 2.0F;
// A rim vertex outside a wedge: colour 0, alpha 0.
constexpr graphics::Rgba kClear{0, 0, 0, 0};
// The pins' and the shapes' distance in front of the overlay camera.
constexpr float kDialDepth = 1.0F;

// The overlay camera the dial is drawn through in the mode of `deviceFlags`. Progressive modes keep the view-window
// scale and take the aspect 1.59 (docs/research/graphics.md#video-mode); Coney's own HUD is always drawn through the
// interlaced 4:3 one (platform::HudLayer).
graphics::OverlayCamera cameraFor(std::uint32_t deviceFlags) {
    constexpr float kProgressiveAspect = 1.59F;
    if ((deviceFlags & kDeviceProgressive) != 0) {
        return graphics::OverlayCamera(1.0F, kProgressiveAspect);
    }
    return graphics::OverlayCamera::forMode((deviceFlags & kDeviceWide) != 0);
}

// Rim vertex `j`'s angle: pi - j x 2 pi / 32 (pi, the first, straight up on screen).
float rimAngle(int j) { return kPi - (static_cast<float>(j) * 2.0F * kPi / static_cast<float>(kLockSegments)); }

// Rim vertex `j` of a shape of radii `radius` (pixels) about `centre`, in the overlay camera's space at the centre's
// depth: the pixel offset turned into overlay units there, y flipped (pixels run down, overlay y up).
graphics::OverlayPoint rimPoint(const graphics::OverlayCamera& camera, graphics::OverlayPoint centre,
                                graphics::LogicalPoint radius, int j) {
    const graphics::LogicalPoint unit = camera.unprojectSize({1.0F, 1.0F}, centre.z);
    const graphics::LogicalPoint offset = LockPickHud::rimOffset(radius, j);
    return graphics::OverlayPoint{centre.x + (offset.x * unit.x), centre.y - (offset.y * unit.y), centre.z};
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

LockPickLayout LockPickHud::layout(std::size_t player, std::uint32_t deviceFlags) {
    const std::size_t p = std::min(player, kPlayers - 1);
    const bool wide = (deviceFlags & kDeviceWide) != 0;
    const bool progressive = (deviceFlags & kDeviceProgressive) != 0;
    // The centre `Init` / `ApplyVideoMode` write for the mode; the shapes share it except in interlaced 4:3, where
    // `Draw` replaces theirs every update. The branch for 0x01 and 0x20 together, which the device never sets, and the
    // PAL rows (flag 0x02, speculative) are left out: Coney has neither.
    const ModeCentres& centres = progressive ? (wide ? kProgressiveWideCentres : kProgressiveCentres)
                                             : (wide ? kInterlacedWideCentres : kInterlacedCentres);
    const ModeCentres& shapes = progressive || wide ? centres : kInterlacedShapeCentres;
    const ModeFactors& f = progressive ? (wide ? kProgressiveWideFactors : kProgressiveFactors)
                                       : (wide ? kInterlacedWideFactors : kInterlacedFactors);
    LockPickLayout layout;
    layout.pinCentre = graphics::OverlayPoint{centres.x.at(p), centres.y, kDialDepth};
    layout.shapeCentre = graphics::OverlayPoint{shapes.x.at(p), shapes.y, kDialDepth};
    layout.wedgeRadius = graphics::LogicalPoint{kLockBandRadius * f.wedgeX, kLockBandRadius * f.wedgeY};
    layout.faceRadius = graphics::LogicalPoint{kLockFaceRadius * f.faceX, kLockFaceRadius * f.faceY};
    layout.pinWidth = f.pinW;
    layout.pinHeight = f.pinH;
    return layout;
}

graphics::LogicalPoint LockPickHud::rimOffset(graphics::LogicalPoint radius, int j) {
    const float a = rimAngle(j);
    return graphics::LogicalPoint{radius.x * std::sin(a), radius.y * std::cos(a)};
}

int LockPickHud::wedgeVertices(float percent) {
    return static_cast<int>(std::floor(static_cast<float>(kLockSegments) * percent / 100.0F / 2.0F));
}

void LockPickHud::renderWedge(graphics::SpriteBatch& batch, const graphics::OverlayCamera& camera,
                              graphics::OverlayPoint centre, graphics::LogicalPoint radius, float percent,
                              graphics::Rgba colour) {
    if (percent <= 0.0F) {
        return;
    }
    // Rim vertex j takes the colour within k of the middle on either side (the dial's mirror flag); the centre always
    // has it, so the segments next to the wedge fade to clear and the rest fade from the centre outwards.
    const int k = wedgeVertices(percent);
    const auto rim = [&](int j) {
        const bool inside = j <= k || kLockSegments - j <= k;
        return graphics::OverlayVertex{rimPoint(camera, centre, radius, j), 0.0F, 0.0F, inside ? colour : kClear};
    };
    const graphics::OverlayVertex middle{centre, 0.0F, 0.0F, colour};
    for (int j = 0; j < kLockSegments; ++j) {
        batch.addTriangle(middle, rim(j), rim(j + 1));
    }
}

void LockPickHud::renderFace(graphics::SpriteBatch& batch, const graphics::OverlayCamera& camera,
                             graphics::OverlayPoint centre, graphics::LogicalPoint radius) {
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
        return graphics::OverlayVertex{rimPoint(camera, centre, radius, j), cu + (ru * std::sin(a + kFaceTurn)),
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
    const LockPickLayout place = layout(player, m_deviceFlags);
    // 1. The pins, largest first, each turned by its angle about the one centre point at distance 1 (`Init` puts them
    //    there over the GUI position); none once every pin is done.
    if (graphics::SpriteBatch* sheet = canvas.minigames; sheet != nullptr &&
                                                         m_current < static_cast<int>(kLockPinCount) &&
                                                         kLockPinRect < sheet->sheet().page.rects.size()) {
        for (std::size_t i = 0; i < kLockPinCount; ++i) {
            const float size = kLockPinSize * (1.0F - (kLockPinShrink * static_cast<float>(i)));
            graphics::Sprite pin;
            pin.position = place.pinCentre;
            pin.width = size * place.pinWidth;
            pin.height = size * place.pinHeight;
            pin.uv = sheet->sheet().page.rect(kLockPinRect);
            pin.colour = kLockPinColour;
            sheet->addSprite(pin, m_angles.at(i));
        }
    }
    // 2. The shapes, after every sprite: band 1 (good), band 2 (perfect) over it, then the lock face over both, their
    //    radii in pixels of the mode's camera.
    const graphics::OverlayCamera camera = cameraFor(m_deviceFlags);
    const auto difficulty = static_cast<std::size_t>(m_difficulty);
    if (canvas.shapes != nullptr) {
        renderWedge(*canvas.shapes, camera, place.shapeCentre, place.wedgeRadius, kLockGoodPercent.at(difficulty),
                    kLockGoodColour);
        renderWedge(*canvas.shapes, camera, place.shapeCentre, place.wedgeRadius, kLockPerfectPercent.at(difficulty),
                    kLockPerfectColour);
    }
    if (canvas.shapeFace != nullptr) {
        renderFace(*canvas.shapeFace, camera, place.shapeCentre, place.faceRadius);
    }
}

} // namespace coney::hud
