// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "animation/anim_math.h"
#include "core/game_random.h"

// A camera's shake and the pad rumble it drives: started by a hit reaction, rage and animation events at one of three
// strengths, eased in over a short time, counted down with the characters' step. Research:
// docs/research/camera.md#shake

namespace coney::camera {

/// One shake strength (docs/research/camera.md#shake).
struct ShakeLevel {
    float amplitude = 0.0F;
    float seconds = 0.0F;
    int rumbleBase = 0; ///< The pad rumble's base: its threshold and cap come from it.
};

/// The three strengths, levels 1 to 3.
inline constexpr std::array<ShakeLevel, 3> kShakeLevels{{{.amplitude = 0.5F, .seconds = 0.10F, .rumbleBase = 0},
                                                         {.amplitude = 0.75F, .seconds = 0.15F, .rumbleBase = 0x30},
                                                         {.amplitude = 1.0F, .seconds = 0.18F, .rumbleBase = 0x60}}};

/// A camera's shake.
class CameraShake {
  public:
    /// The amplitude's ease toward the level's, a share an update.
    static constexpr float kEase = 0.65F;
    /// Shakes started while the combat camera is on are this strong.
    static constexpr float kCombatScale = 0.66F;
    /// While the combat camera is on, the rumble's threshold is lowered by a quarter.
    static constexpr float kCombatThresholdScale = 0.75F;
    /// The time counts down by `dt × min(1, kStepScale × step)`, `step` the characters' step: slower in slow motion.
    static constexpr float kStepScale = 54.0F;
    /// **Coney's stand-in** for the view offset's form, which is not traced: each axis a random share in [-1, 1] of
    /// the amplitude times this, metres.
    static constexpr float kOffsetScale = 0.05F;

    /// Starts a shake of `level` (1 to 3; 0 stops one, others do nothing), weaker while `combat` (the combat camera is
    /// on).
    /// @orig 0x001210f8 Cam_StartShake (Cam_ICamera.cpp)
    void start(int level, bool combat);

    /// One update of `dt` seconds with the characters' step `step` (1/30 s, less in slow motion): the amplitude eases
    /// toward the level's while the time lasts (**Coney's reading**: toward 0 after it), the time counts down, the pad
    /// rumble and a new view offset follow the amplitude.
    /// @orig 0x00121298 Cam_UpdateShake (Cam_ICamera.cpp)
    void update(float dt, float step);

    /// The amplitude now.
    [[nodiscard]] float amplitude() const { return m_amplitude; }
    /// The pad's rumble byte (pad record `+0x41`): `255 × amplitude / the level's` when that is above the level's
    /// threshold `(base >> 2) + 0x28` (a quarter lower in combat), at most `base + 0x60`; 0 otherwise.
    [[nodiscard]] std::uint8_t rumble() const { return m_rumble; }
    /// The view offset this update, added only while `CamEnable(6)` is on.
    [[nodiscard]] anim::Vec3 offset() const { return m_offset; }

  private:
    float m_target = 0.0F; // the level's amplitude
    float m_amplitude = 0.0F;
    float m_time = 0.0F;
    int m_rumbleBase = 0;
    bool m_combat = false;
    std::uint8_t m_rumble = 0;
    anim::Vec3 m_offset;
    GameRandom m_random; // the stand-in generator: the same offsets on every run
};

} // namespace coney::camera
