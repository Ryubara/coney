// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "animation/anim_math.h"
#include "effects/particle_types.h"

namespace coney::effects {

/// A camera's view window, for the effects that test whether a point is in view: its unit axes and the tangents of
/// its half angles (the 4:3 picture of its field of view), and its clip distances.
struct ViewWindow {
    anim::Vec3 forward{0.0F, 1.0F, 0.0F};
    anim::Vec3 right{1.0F, 0.0F, 0.0F};
    anim::Vec3 up{0.0F, 0.0F, 1.0F};
    float tanHalfWidth = 1.0F;
    float tanHalfHeight = 0.75F;
    float nearClip = 0.1F;
    float farClip = 115.0F;
};

/// Whether `point` lies no more than `margin` metres outside the frustum of a camera at `eye` with `window`: its
/// signed distance to each of the six planes is at least -margin (`Cameras_IsPointVisibleAny`,
/// docs/research/particles.md#fog).
[[nodiscard]] bool nearView(anim::Vec3 eye, const ViewWindow& window, anim::Vec3 point, float margin);

/// Where a view's camera is and what it looks at, for the effects that follow the camera; and its view window when
/// known (none: every point counts as in view).
struct EffectsViewer {
    anim::Vec3 position;
    anim::Vec3 target;
    std::optional<ViewWindow> window;
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

/// The drifting ground fog of a view (docs/research/particles.md#fog): the `part_fog` emitter `Start3DFog` makes in
/// each player's screen-effects manager, which every 5 frames tops its view up to 20 live `sub_fog` wisps (at most 10
/// at a time), each placed at random within 20 m of the camera's target and 0.5-2 m above it. A wisp drifts from birth
/// toward the camera, aimed up to 2 m to either side along the camera's x axis, at `drift` × 1.75-2.25 metres a
/// second (the unit inferred), and is a camera-facing square of 2 × a random 1-4 that grows from 0 over its first
/// update.
///
/// A wisp updates every 2 ticks (60 a second), or every 30 with an alpha step of 1 when its step comes out 0. Each
/// update: the current alpha and size become the previous ones; while its age (updates) is below the fade length
/// (⌊9 / fadeSpeed⌋, at least 1) and its alpha below the colour's, the alpha rises by ⌊alpha / steps⌋ × ⌊fadeRate⌋;
/// then it is hidden (alpha 0, updating every tick) more than 20 m from the camera or more than 5 m outside its view,
/// and hidden (updating every 30 ticks) within 4 m; from its third update a wisp whose previous alpha was 0 ends. It
/// is drawn with its previous and current alpha and size blended by the time since its last update. The sprite
/// word's high half is a sheet record: `0x212` is `part_fog_00`, `0x213` `part_fog_01`; rectangle 0, the whole
/// texture. A frame draws at most 10 wisps, the first in the pool's order, hidden ones taking their place.
///
/// **Coney's stand-ins** where the page is silent: the generator is the fog's own xorshift32 (not the particles'
/// stream), so a run is the same every time; the camera flag that hides wisps within 10 m is not modelled. Coney has
/// one view, so one emitter.
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
    /// Wisps are placed within this many metres of the camera's target, and hidden beyond it from the camera.
    static constexpr float kRadius = 20.0F;
    /// A wisp's height above the target, metres.
    static constexpr float kMinHeight = 0.5F;
    static constexpr float kMaxHeight = 2.0F;
    /// A wisp nearer the camera than this is hidden.
    static constexpr float kHideWithin = 4.0F;
    /// A wisp more than this far outside the camera's view is hidden.
    static constexpr float kViewMargin = 5.0F;
    /// A wisp's update interval, ticks: normally, with a 0 alpha step (and while hidden near), and while hidden far
    /// or out of view.
    static constexpr int kUpdateTicks = 2;
    static constexpr int kSlowUpdateTicks = 30;
    static constexpr int kHiddenUpdateTicks = 1;
    /// A wisp's size is a random kMinSize-kMaxSize; it is drawn kDrawnScale times that across.
    static constexpr float kMinSize = 1.0F;
    static constexpr float kMaxSize = 4.0F;
    static constexpr float kDrawnScale = 2.0F;
    /// The wisps a frame draws.
    static constexpr std::size_t kDrawnPerFrame = 10;

    /// One fog wisp.
    struct Wisp {
        anim::Vec3 position;
        anim::Vec3 velocity;            ///< Metres a second.
        float size = 0.0F;              ///< Its size now (`+0xc0`), 1-4.
        float previousSize = 0.0F;      ///< At its last update (`+0xbc`); 0 at birth.
        std::uint8_t alpha = 0;         ///< Its alpha now (`+0xb4`), 0-255.
        std::uint8_t previousAlpha = 0; ///< At its last update (`+0xb0`).
        std::uint32_t age = 0;          ///< Updates done.
        int interval = kUpdateTicks;    ///< Ticks between its updates.
        float sinceUpdate = 0.0F;       ///< Ticks since its last update.
        bool hidden = false;            ///< Hidden by its last update.
    };

    /// One wisp as drawn: where, its colour `0xRRGGBBAA` and its size across.
    struct Drawn {
        anim::Vec3 position;
        std::uint32_t colour = 0;
        float size = 0.0F;
    };

    explicit GroundFog(std::uint32_t seed = 0x6C078965U) : m_random(seed != 0 ? seed : 1U) {}

    /// `Start3DFog`: any fog running is killed (message `0x15`) and a new emitter started with `settings`.
    /// @orig 0x0018e148 Fog3D_Start (unknown)
    void start(const FogSettings& settings);
    /// Ends the fog: no emitter, no wisps.
    void stop();
    /// `MaxFogParticles(count)`: the wisps a view keeps; nothing with no fog running.
    void setMaxWisps(std::size_t count);

    /// One step of `seconds` seen from `viewer`: the top-ups due, the wisps' drift and their updates.
    /// @orig 0x003cadd8 Fog3D_EmitterUpdate (unknown)
    void step(float seconds, const EffectsViewer& viewer);

    /// The settings, while fog runs.
    [[nodiscard]] const std::optional<FogSettings>& settings() const { return m_settings; }
    /// The live wisps.
    [[nodiscard]] const std::vector<Wisp>& wisps() const { return m_wisps; }
    /// The wisps a view keeps.
    [[nodiscard]] std::size_t maxWisps() const { return m_maxWisps; }
    /// The alpha a wisp adds per update, the updates its fade lasts and its update interval, ticks.
    [[nodiscard]] std::uint8_t alphaStep() const { return m_alphaStep; }
    [[nodiscard]] std::uint32_t fadeUpdates() const { return m_fadeUpdates; }
    [[nodiscard]] int updateTicks() const { return m_baseInterval; }

    /// The sheet of the wisps' sprite word: `part_fog_01` for record 531, else `part_fog_00` (record 530, which 15 of
    /// the 17 calls pass).
    [[nodiscard]] static ParticleSheet sheetOf(std::uint32_t sprite);

    /// The wisps drawn this frame: the first kDrawnPerFrame of the pool, less those whose blended alpha is 0.
    [[nodiscard]] std::vector<Drawn> drawn() const;

  private:
    // A number in [0, 1) from the generator.
    float unit();
    // One wisp at random round `viewer`'s target, drifting toward the camera at `drift` × 1.75-2.25 (sub_fog's init).
    // @orig 0x003ca658 Fog3D_WispInit (unknown)
    Wisp makeWisp(const EffectsViewer& viewer, float drift);
    // One update of `wisp` seen from `viewer`; false when it ends.
    // @orig 0x003ca9d8 Fog3D_WispUpdate (unknown)
    [[nodiscard]] bool update(Wisp& wisp, const EffectsViewer& viewer) const;

    std::optional<FogSettings> m_settings;
    std::vector<Wisp> m_wisps;
    std::size_t m_maxWisps = kDefaultMaxWisps;
    float m_frames = 0.0F; // frames since the last top-up
    float m_ticks = 0.0F;  // ticks of game time not yet run
    std::uint8_t m_alphaStep = 1;
    std::uint32_t m_fadeUpdates = 1;
    int m_baseInterval = kUpdateTicks;
    std::uint32_t m_random;
};

} // namespace coney::effects
