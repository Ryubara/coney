// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/camera_litter.h"

#include <algorithm>
#include <cmath>

namespace coney::effects {

namespace {

// A kind's rectangles of `part_page1` and size bytes (both ranges include their ends).
struct KindLook {
    int firstRect;
    int lastRect;
    int minSize;
    int maxSize;
};
constexpr std::array<KindLook, CameraLitter::kKinds> kKindLooks{KindLook{54, 58, 96, 102}, KindLook{30, 32, 96, 102},
                                                                KindLook{8, 11, 16, 32}, KindLook{18, 18, 43, 50}};
// The grid starts this many cells before the camera on x and y (−28 m).
constexpr float kStartCells = 4.0F;
// A size byte is 1/256 m (the card is 1 × 1 before scaling; inferred).
constexpr float kSizeUnit = 1.0F / 256.0F;
// The grey, the wind threshold and the lifetime's ranges.
constexpr int kMinGrey = 128;
constexpr int kMaxGrey = 190;
constexpr float kMinThreshold = 0.5F;
constexpr float kMaxThreshold = 10.0F;
constexpr int kMinLifetime = 600;
constexpr int kMaxLifetime = 900;
// A fading piece's alpha is this × its countdown.
constexpr float kFadeAlpha = 4.25F;
// Opaque.
constexpr float kOpaque = 255.0F;
// Lying down takes up to this many updates.
constexpr int kFlattenUpdates = 8;
// Below this a length is taken as none.
constexpr float kTiny = 1e-6F;

} // namespace

float LitterPiece::alpha() const { return fade > 0 ? kFadeAlpha * static_cast<float>(fade) : kOpaque; }

float CameraLitter::unit() {
    // xorshift32, from the litter's own seed.
    m_random ^= m_random << 13U;
    m_random ^= m_random >> 17U;
    m_random ^= m_random << 5U;
    constexpr std::uint32_t kSteps = 1U << 16U;
    return static_cast<float>(m_random % kSteps) / static_cast<float>(kSteps);
}

int CameraLitter::between(int low, int high) {
    return low + std::min(high - low, static_cast<int>(unit() * static_cast<float>(high - low + 1)));
}

void CameraLitter::start(std::uint32_t kind) {
    if (kind >= kKinds) {
        return;
    }
    m_kind = kind;
    m_armed = false;
    m_updates = 0.0F;
}

void CameraLitter::end() {
    m_kind.reset();
    m_armed = false;
}

void CameraLitter::place(std::size_t index, anim::Vec3 camera) {
    const KindLook& look = kKindLooks.at(*m_kind);
    LitterPiece& piece = m_pieces.at(index);
    const auto column = static_cast<float>(static_cast<int>(index) % kGrid);
    // Whole cells: the row is the index's quotient by the grid's width.
    const int cellRow = static_cast<int>(index) / kGrid;
    const auto row = static_cast<float>(cellRow);
    const float start = -kStartCells * kCell;
    const std::uint32_t stagger = piece.rayCountdown;
    piece = LitterPiece{};
    piece.position = anim::Vec3{camera.x + start + (column * kCell), camera.y + start + (row * kCell),
                                camera.z + kMinHeight + ((kMaxHeight - kMinHeight) * unit())};
    piece.rect = static_cast<std::uint16_t>(between(look.firstRect, look.lastRect));
    piece.size = static_cast<float>(between(look.minSize, look.maxSize)) * kSizeUnit;
    piece.grey = static_cast<std::uint8_t>(between(kMinGrey, kMaxGrey));
    piece.threshold = kMinThreshold + ((kMaxThreshold - kMinThreshold) * unit());
    piece.lifetime = static_cast<std::uint32_t>(between(kMinLifetime, kMaxLifetime));
    piece.placed = true;
    // The ground rays are staggered over the pieces; a respawn keeps its place in the cycle.
    piece.rayCountdown = m_armed ? stagger : static_cast<std::uint32_t>(index) % kRayUpdates;
}

void CameraLitter::update(std::size_t index, anim::Vec3 camera, const LitterRay& ray) {
    LitterPiece& piece = m_pieces.at(index);
    // A fading piece counts down, then comes back round the camera.
    if (piece.fade > 0) {
        if (--piece.fade == 0) {
            place(index, camera);
        }
        return;
    }
    // Left behind by the camera, or out of life once landed: fade out.
    const bool far =
        std::abs(piece.position.x - camera.x) > kKeepWithin || std::abs(piece.position.y - camera.y) > kKeepWithin;
    if (piece.lifetime > 0) {
        --piece.lifetime;
    }
    if (far || (piece.grounded && piece.lifetime == 0)) {
        piece.fade = kFadeUpdates;
        return;
    }
    // The ground ray: none below makes it respawn; a piece just placed drops onto the hit.
    if (piece.rayCountdown == 0) {
        piece.rayCountdown = kRayUpdates;
        if (ray) {
            const std::optional<LitterHit> ground =
                ray(piece.position, anim::Vec3{piece.position.x, piece.position.y, piece.position.z - kGroundRay});
            if (!ground) {
                place(index, camera);
                return;
            }
            if (piece.placed) {
                piece.position = ground->point;
                piece.placed = false;
            }
        }
    }
    --piece.rayCountdown;
    // Gravity: full edge-on, half lying flat.
    piece.velocity.z -= kGravity * (1.0F - (0.5F * (1.0F - piece.tilt)));
    // The collision ray along the velocity from just behind the piece.
    const float speed = std::sqrt(anim::dot(piece.velocity, piece.velocity));
    if (ray && speed > kTiny) {
        const anim::Vec3 direction = anim::scale(piece.velocity, 1.0F / speed);
        const anim::Vec3 from = anim::subtract(piece.position, anim::scale(direction, kRayBehind));
        const anim::Vec3 to =
            anim::add(piece.position, anim::scale(direction, (speed / kUpdatesPerSecond) + kHitWithin));
        if (const std::optional<LitterHit> hit = ray(from, to)) {
            // The velocity's part into the surface goes; the piece lies down on it (Coney's reading: at the hit, so
            // it does not hang above the ground).
            const float into = anim::dot(piece.velocity, hit->normal);
            if (into < 0.0F) {
                piece.velocity = anim::subtract(piece.velocity, anim::scale(hit->normal, into));
            }
            piece.position = hit->point;
            if (!piece.grounded) {
                piece.flattening = piece.tilt / static_cast<float>(between(1, kFlattenUpdates));
            }
            piece.grounded = true;
        }
    }
    piece.tilt = std::max(0.0F, piece.tilt - piece.flattening);
    piece.position = anim::add(piece.position, anim::scale(piece.velocity, 1.0F / kUpdatesPerSecond));
}

void CameraLitter::step(float seconds, anim::Vec3 camera, const LitterRay& ray) {
    if (!m_kind) {
        return;
    }
    if (!m_armed) {
        for (std::size_t i = 0; i < kPieces; ++i) {
            place(i, camera);
        }
        m_armed = true;
    }
    m_updates += seconds * kUpdatesPerSecond;
    while (m_updates >= 1.0F) {
        m_updates -= 1.0F;
        for (std::size_t i = 0; i < kPieces; ++i) {
            update(i, camera, ray);
        }
    }
}

} // namespace coney::effects
