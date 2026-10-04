// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace coney::camera {

/// What a game camera sees through: its field of view and clip distances. The player camera's draw distance, which
/// the world pass moves each frame, is capped by `farClip` (docs/research/world.md#a-frame).
///
/// Research: docs/research/world.md#player-camera
struct CameraLens {
    float fieldOfView = 60.0F; ///< Horizontal, in degrees, on a 4:3 picture (+0x44).
    float nearClip = 0.3F;     ///< (+0x50).
    float farClip = 60.0F;     ///< The camera's own far clip (+0x54).
};

/// The base camera's lens (`Cam_ICamera`, 0x00120868): 60°, near 0.3, far 60.
inline constexpr CameraLens kBaseCameraLens{.fieldOfView = 60.0F, .nearClip = 0.3F, .farClip = 60.0F};

/// The player camera's lens (`Cam_Follow`, 0x00124778): 65°, near 0.1, far 115.
inline constexpr CameraLens kPlayerCameraLens{.fieldOfView = 65.0F, .nearClip = 0.1F, .farClip = 115.0F};

/// Half a view window at distance 1, as RenderWare's view window is given.
struct ViewWindow {
    float halfWidth = 0.0F;
    float halfHeight = 0.0F;
};

/// The view window of `lens` on the 4:3 picture: tan(fov / 2) wide and three quarters of that high, (0.637, 0.478)
/// for the player camera. The original computes tan(fov / 2) × 0.75 × aspect with its aspect 4/3, so the width is
/// the tangent itself. Split screen halves it (not used by Coney yet).
///
/// Research: docs/research/world.md#player-camera (0x00120a98)
[[nodiscard]] ViewWindow viewWindow(const CameraLens& lens);

} // namespace coney::camera
