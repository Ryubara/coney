// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "animation/anim_math.h"

namespace coney::effects {

/// Where a view's camera is and what it looks at, for the effects that follow the camera.
struct EffectsViewer {
    anim::Vec3 position;
    anim::Vec3 target;
};

/// `Start3DFog(texture, colour, drift, fadeSpeed, fadeRate)`'s settings
/// (docs/references/bindings/effects.md#start3dfog).
struct FogSettings {
    std::uint32_t sprite = 0; ///< The wisp's sprite word: sheet in the top 16 bits, rectangle in the low 16.
    std::array<std::uint8_t, 4> colour{}; ///< `{r, g, b, a}`; a wisp fades in to the alpha.
    float drift = 0.0F;                   ///< Each wisp drifts at this × 1.75-2.25.
    float fadeSpeed = 1.0F;               ///< The fade-in takes 9 / fadeSpeed steps (at least 1).
    float fadeRate = 1.0F;                ///< A whole-number multiplier on the opacity added per fade step.
};

/// The drifting ground fog of a view: the `part_fog` emitter `Start3DFog` makes in each player's screen-effects
/// manager, which every 5 frames tops its view up to 20 live `sub_fog` wisps (at most 10 at a time), each placed at
/// random within 20 m of the camera's target and 0.5-2 m above it, drifting and fading in to the colour's alpha; a wisp
/// is dropped when its viewer is more than 20 m away and hidden within 4 m. A wisp drifts from birth toward the camera,
/// aimed up to 2 m to either side along the camera's x axis, at `drift` × 1.75-2.25 metres a second (the unit
/// inferred). The sprite word's high half is a sheet record: `0x212` is `part_fog_00`, `0x213` `part_fog_01`.
///
/// **Coney's stand-ins** where the page is silent: a fade step is a frame, each adding alpha × fadeRate / steps; the
/// wisps are kept, not drawn (Coney's renderer does not load the fog sheets yet). Coney has one view, so one emitter.
/// The generator is the fog's own xorshift32, so a run is the same every time.
///
/// Research: docs/references/bindings/effects.md#start3dfog, docs/research/particles.md#fog
class GroundFog {
  public:
    /// The wisps a view keeps alive unless `MaxFogParticles` says otherwise.
    static constexpr std::size_t kDefaultMaxWisps = 20;
    /// At most this many wisps made by one top-up.
    static constexpr std::size_t kWispsPerTopUp = 10;
    /// Frames (60 a second) between top-ups.
    static constexpr std::uint32_t kTopUpFrames = 5;
    /// Wisps are placed within this many metres of the camera's target, and dropped beyond it from the camera.
    static constexpr float kRadius = 20.0F;
    /// A wisp's height above the target, metres.
    static constexpr float kMinHeight = 0.5F;
    static constexpr float kMaxHeight = 2.0F;
    /// A wisp nearer the camera than this is hidden.
    static constexpr float kHideWithin = 4.0F;

    /// One fog wisp.
    struct Wisp {
        anim::Vec3 position;
        anim::Vec3 velocity; ///< Metres a second.
        float alpha = 0.0F;  ///< Opacity now, 0-255.
        bool hidden = false; ///< Within kHideWithin of the camera.
    };

    explicit GroundFog(std::uint32_t seed = 0x6C078965U) : m_random(seed != 0 ? seed : 1U) {}

    /// `Start3DFog`: any fog running is killed (message `0x15`) and a new emitter started with `settings`.
    /// @orig 0x0018e148 Fog3D_Start (unknown)
    void start(const FogSettings& settings);
    /// Ends the fog: no emitter, no wisps.
    void stop();
    /// `MaxFogParticles(count)`: the wisps a view keeps; nothing with no fog running.
    void setMaxWisps(std::size_t count);

    /// One step of `seconds` seen from `viewer`: the top-ups due, the wisps' drift and fade, the far ones dropped.
    /// @orig 0x003cadd8 Fog3D_EmitterUpdate (unknown)
    void step(float seconds, const EffectsViewer& viewer);

    /// The settings, while fog runs.
    [[nodiscard]] const std::optional<FogSettings>& settings() const { return m_settings; }
    /// The live wisps.
    [[nodiscard]] const std::vector<Wisp>& wisps() const { return m_wisps; }
    /// The wisps a view keeps.
    [[nodiscard]] std::size_t maxWisps() const { return m_maxWisps; }

  private:
    // A number in [0, 1) from the generator.
    float unit();
    // One wisp at random round `viewer`'s target, drifting toward the camera at `drift` × 1.75-2.25 (sub_fog's init).
    // @orig 0x003ca658 Fog3D_WispInit (unknown)
    Wisp makeWisp(const EffectsViewer& viewer, float drift);

    std::optional<FogSettings> m_settings;
    std::vector<Wisp> m_wisps;
    std::size_t m_maxWisps = kDefaultMaxWisps;
    float m_frames = 0.0F; // frames since the last top-up
    std::uint32_t m_random;
};

} // namespace coney::effects
