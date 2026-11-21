// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "graphics/level_lighting.h"
#include "graphics/light_manager.h"
#include "graphics/render_device.h"

namespace coney::graphics {

/// The lighting lab's views (docs/guides/realtime-lighting.md#lab): how a frame's atomics are lit for the picture.
/// The design's other views (runtime light only, the runtime share, saturation, lights per atomic) need the prelight
/// apart from the runtime light, which librw bakes together when it instances a model, so they wait for Coney's own
/// shaders (renderer item 22).
enum class LightingView : std::uint8_t {
    Final,        ///< As the game lights it.
    PrelightOnly, ///< No runtime light: the baked colour alone.
};

/// The lighting lab's layers: each a switch on the drawing side, all on by default (the faithful picture).
struct LightingLayers {
    bool worldAmbient = true; ///< Light A, the world ambient.
    bool objectLights = true; ///< The ambient and directional lights other than light A (the objects', the world's).
    bool pointLights = true;  ///< Point and spot lights, the glow among them.
    bool flicker = true;      ///< Off: flickering lights shine with their base colour.
    bool brightness = true;   ///< Off: ambient and directional lights without the brightness and colour offset.
    bool coronas = true;      ///< The corona sprites.
    bool blobShadows = true;  ///< The humans' blob shadows.

    friend bool operator==(const LightingLayers&, const LightingLayers&) = default;
};

/// The World page's fog (docs/guides/debug-menu.md#world): drawing-side overrides of the level's own fog, each none
/// (the default) for the level's value.
struct FogSwitches {
    std::optional<Rgba> colour; ///< In place of the level's colour (`SetFogColor`), which is also the background.
    std::optional<float> start; ///< In place of the level's start (`SetFogDistance`), a fraction of the far clip.

    friend bool operator==(const FogSwitches&, const FogSwitches&) = default;
};

/// The most a fog start can be: just short of the far clip, where the fog plane would meet the far plane and the fog's
/// slope divide by nothing. The sandbox's fog plane keeps the same margin (sandbox_renderer.cpp).
inline constexpr float kMaxFogStart = 0.99F;

/// The fog a frame is drawn with: `level`'s, with `switches`' colour and start in place of its own where set, the
/// switches' start held between 0 and kMaxFogStart (the level's own is left as its scripts set it).
/// @coney-only the World page's fog (docs/guides/debug-menu.md#world)
[[nodiscard]] WorldFog shownFog(const WorldFog& level, const FogSwitches& switches);

/// The World page's parts of the scene (docs/guides/debug-menu.md#world): each a switch on the drawing side, all on by
/// default (the faithful picture). The coronas and the blob shadows are the lighting layers' (LightingLayers), which
/// the page shows among these.
struct WorldParts {
    /// The streamed world's sectors; off also hides the background, which the mesh views' hook sees as the same kind
    /// of model (scenery).
    bool scenery = true;
    bool background = true; ///< The sky box, the cloud box, the skyline and the level world's light glows.
    bool humans = true;     ///< The humans, with what they wear and carry.
    bool objects = true;    ///< The world objects, the doors and the cars.
    bool glass = true;      ///< The glass panes.
    bool water = true;      ///< The water.
    bool effects = true;    ///< The particles, the glints and the throw-aim arc.
    bool rings = true;      ///< The health rings and the L1 markers.

    friend bool operator==(const WorldParts&, const WorldParts&) = default;
};

/// What the lighting lab and the World page switch on the drawing side of the lighting frame
/// (docs/guides/realtime-lighting.md#lab, docs/guides/debug-menu.md#world). Read by the renderer's lighting rig and the
/// play mode's drawing only, never by the simulation; the defaults draw the faithful picture, and test mode never
/// changes them.
struct LightingSwitches {
    /// Keep the light view and the time of the step it was held on (the flicker, the pulse and every selection) while
    /// no step runs; a step moves it on by one.
    bool hold = false;
    /// Cull the lights from the game camera (the follow, scene or pinned camera) while the free camera flies, as the
    /// original culls from the player's camera; off: from the camera the frame is drawn through. The caps, read from
    /// each atomic's distance in the ported draw functions, stay the drawn camera's either way.
    bool lightFromGameCamera = true;
    LightingLayers layers;
    LightingView view = LightingView::Final;
    FogSwitches fog;  ///< The World page's fog.
    WorldParts parts; ///< The World page's parts.

    /// Whether these draw the faithful picture: every layer and part on, the final view and the level's fog (hold and
    /// the camera aside).
    /// @coney-only the lighting lab's and the World page's switches
    [[nodiscard]] bool faithfulPicture() const {
        return layers == LightingLayers{} && view == LightingView::Final && fog == FogSwitches{} &&
               parts == WorldParts{};
    }
};

/// The lights of `selection` that `layers` and `view` keep for the draw, in order; the selection itself is untouched
/// (the lighting frame records it as the game made it).
/// @coney-only the lighting lab's layers and views (docs/guides/realtime-lighting.md#lab)
[[nodiscard]] LightSelection layeredSelection(const LightSelection& selection, const LightManager& manager,
                                              const LightingLayers& layers, LightingView view);

/// The colour record `index` is drawn with under `layers`: its current colour, its base colour for a point or spot
/// light with the flicker layer off, its colour without the brightness and offset with that layer off.
/// @coney-only the lighting lab's layers (docs/guides/realtime-lighting.md#lab)
[[nodiscard]] LightColour layeredColour(const LightManager& manager, std::uint16_t index, const LightingLayers& layers);

/// The light view and game time a frame is lit at.
struct LitMoment {
    LightView view;
    std::uint64_t nowMs = 0;
};

/// *Hold lighting*: the moment the frame is lit at. Off, the moment asked for; on, the moment of the last frame of
/// the step it was held on, so a paused game keeps one frame's lighting while the free camera flies, and each step
/// that runs moves it on.
/// @coney-only the lighting lab's Hold lighting (docs/guides/realtime-lighting.md#lab)
class LightingHold {
  public:
    /// A simulation step ran.
    void stepped() { ++m_steps; }
    /// The moment to light this frame at: `wanted`, or the held one while `hold` is on and no step ran since.
    [[nodiscard]] LitMoment choose(const LitMoment& wanted, bool hold);

  private:
    std::optional<LitMoment> m_held; // the last moment chosen
    std::uint64_t m_heldAt = 0;      // the step count it was chosen at
    std::uint64_t m_steps = 0;       // steps run
};

} // namespace coney::graphics
