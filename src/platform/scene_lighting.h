// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "graphics/frame_description.h"
#include "graphics/human_lighting.h"
#include "graphics/level_lighting.h"
#include "graphics/light_manager.h"
#include "graphics/lighting_frame.h"
#include "graphics/lighting_switches.h"
#include "graphics/particle_page.h"
#include "hud/health_rings.h"
#include "platform/mesh_view_hook.h"
#include "world/view_frustum.h"
#include "world_objects/glass.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Light;
struct World;
} // namespace rw

namespace coney::platform {

/// Draws atomics lit the way the original's LightManager lights them: per atomic, the lights
/// graphics::LightManager::select() chooses, handed to librw (whose GL3 lighting adds ambient, directional and point
/// lights to the prelighting and clamps, as the PS2 pipeline does); then the frame's coronas from the `lighting`
/// sheet and the humans' blob shadows from `part_page1`, as camera-facing and ground-lying quads.
///
/// librw lights an atomic from the current world's lights, and its point lights only when the atomic is in that
/// world. So the rig owns a librw world holding just the selected lights of the atomic being drawn, and puts the
/// atomic in it for the draw. One librw light mirrors each manager record that is drawn.
///
/// Needs a running RenderEngine and must be destroyed before it stops. Not copyable or movable: librw keeps pointers.
///
/// Research: docs/research/lighting.md
class SceneLighting {
  public:
    /// Lights from `lighting`, which must outlive the rig. `coronas` and `shadows` are the `lighting` and `part_page1`
    /// sheets (converted for drawing); without them the coronas or shadows are not drawn.
    SceneLighting(graphics::LevelLighting& lighting, std::optional<graphics::SpriteSheet> coronas,
                  std::optional<graphics::SpriteSheet> shadows);
    ~SceneLighting();
    SceneLighting(const SceneLighting&) = delete;
    SceneLighting& operator=(const SceneLighting&) = delete;
    SceneLighting(SceneLighting&&) = delete;
    SceneLighting& operator=(SceneLighting&&) = delete;

    /// The start of a frame through a camera at `pose` (RenderWare's axes) with clip distances `nearClip` and
    /// `farClip`, at game time `nowMs`: the manager's viewport pass (the ambients, the cull, the coronas), then the
    /// lighting frame (lightingFrame()). With the lighting lab's switches set (setSwitches()), the lights are culled
    /// from the game camera's view (setGameView()) under *Light from*, and *Hold lighting* keeps the moment of the
    /// step it holds; pose() stays `pose` either way.
    void beginFrame(const world::CameraPose& pose, float nearClip, float farClip, std::uint64_t nowMs);

    /// Draws a streamed sector's atomic with the world's lights (list A and the world's point lights it overlaps).
    /// @orig 0x00411990 World_RenderSectorAtomic (WorldPS2.cpp)
    void drawWorldAtomic(rw::Atomic* atomic);
    /// The lights a streamed sector's atomic is drawn with (list A and the world's point lights it overlaps), by value,
    /// as drawWorldAtomic() would put them in librw's world, with the lighting lab's layers applied: for the frame
    /// description's draw of the sector (docs/engine/renderer.md#frame-description). Counts the atomic as drawn and
    /// adds the lighting frame's row of its selection, as drawWorldAtomic() does.
    [[nodiscard]] std::vector<graphics::SceneLight> worldLights(rw::Atomic* atomic);
    /// The lights a world object's or car part's atomic is drawn with (the objects' lights for its squared distance to
    /// the camera and the point lights its sphere overlaps), by value, as drawObjectAtomic() would put them in librw's
    /// world, with the lighting lab's layers applied: for the frame description's draw of it (drawObject()). Counts the
    /// atomic as drawn and adds the lighting frame's row of its selection, as drawObjectAtomic() does.
    [[nodiscard]] std::vector<graphics::SceneLight> objectLights(rw::Atomic* atomic);
    /// Whether the debug tools' mesh views are set (setMeshViews()): they decide per atomic what is drawn, so the world
    /// renderer then draws its sectors through drawWorldAtomic().
    /// @coney-only the debug tools' mesh views (docs/guides/debug-menu.md#mesh-views); the original has none
    [[nodiscard]] bool meshViewsOn() const { return m_meshViews != nullptr; }
    /// Draws the background's atomics with the world's ambient and directional lights only.
    void drawBackgroundAtomic(rw::Atomic* atomic);
    /// Draws a human's atomic: the objects' lights for his squared distance to the camera, and the point lights his
    /// sphere overlaps unless `hidden` (in a shadow). Colour dimming is the caller's (the mesh's colour).
    /// @orig 0x00174320 HumanRender_Draw (HumanRender.cpp)
    void drawHumanAtomic(rw::Atomic* atomic, bool hidden);

    /// Draws this frame's coronas: Z test on, no Z write, no fog, blended by alpha.
    /// @orig 0x0017caa8 Light_UpdateFlickerAndCorona (LightManager.cpp)
    void drawCoronas() const;
    /// Draws one blob shadow (game axes) per entry of `shadows`.
    void drawBlobShadows(std::span<const graphics::BlobShadow> shadows) const;
    /// Draws a world object's atomic with the objects' lights for its squared distance to the camera and the point
    /// lights its sphere overlaps, as a human's (docs/research/objects.md#models: the lights are chosen per object).
    /// @orig 0x0017fd78 ObjectRender_Draw (unknown)
    void drawObjectAtomic(rw::Atomic* atomic);
    /// Draws the glass panes (game axes) from `part_page1`, each a quad of its rectangle in its colour: alpha blended,
    /// Z tested, no Z write, no culling, no fog.
    /// @orig 0x001831c0 Instance_DrawOneSpriteIm3D (unknown)
    void drawGlass(std::span<const world_objects::GlassQuad> panes) const;
    /// Draws the health rings and the L1 markers (game axes) from `part_page1`: triangle fans in their per-vertex
    /// colours, Z tested, fog off. **Coney's stand-in** for the state the world pass leaves (not traced): alpha
    /// blended, no Z write.
    /// @orig 0x0017b2e0 GroundRing_DrawQueued (unknown)
    void drawGroundRings(std::span<const hud::GroundRing> rings, std::span<const hud::TargetMarker> markers) const;

    /// The lighting lab's switches to draw with (docs/guides/realtime-lighting.md#lab), which must outlive their use;
    /// null (the default): the faithful picture, lit from the frame's own camera, as without the debug tools.
    /// @coney-only the lighting lab (docs/guides/realtime-lighting.md#lab); the original has none
    void setSwitches(const graphics::LightingSwitches* switches) { m_switches = switches; }
    /// The game camera's view (the follow, scene or pinned camera) for the next beginFrame()'s *Light from*; none:
    /// the frame's own camera is the game's.
    /// @coney-only the lighting lab's Light from (docs/guides/realtime-lighting.md#lab)
    void setGameView(const std::optional<graphics::LightView>& view) { m_gameView = view; }
    /// A simulation step ran: *Hold lighting* moves on to the next frame's moment.
    /// @coney-only the lighting lab's Hold lighting (docs/guides/realtime-lighting.md#lab)
    void stepped() { m_hold.stepped(); }
    /// Whether the human atomics drawn from now on are hidden in a shadow (their selection looks for no point light),
    /// for the lighting frame's rows of them; the caller of drawHumanAtomic() says so before each human.
    /// @coney-only the lighting frame's selection rows
    void noteHiddenHuman(bool hidden) { m_hiddenHuman = hidden; }
    /// Whether the humans' blob shadows are drawn: off only under the lighting lab's layer.
    /// @coney-only the lighting lab's layers (docs/guides/realtime-lighting.md#lab)
    [[nodiscard]] bool drawsBlobShadows() const { return m_switches == nullptr || m_switches->layers.blobShadows; }

    /// Hands every atomic drawn from now on to the debug tools' mesh views (`hook`, which must outlive its use), or to
    /// nobody (null: each is drawn as it always is, the hook not consulted).
    /// @coney-only the debug tools' mesh views (docs/guides/debug-menu.md#mesh-views); the original has none
    void setMeshViews(MeshViewHook* hook) { m_meshViews = hook; }
    /// Marks `atomic` as a second layer of the model drawn around it (a human's blood pass), which the mesh views draw
    /// only with the shaded draw; null: none. The caller marks it, so the original's draw functions stay as they are.
    /// @coney-only the debug tools' mesh views (docs/guides/debug-menu.md#mesh-views); the original has none
    void setMeshLayer(const rw::Atomic* atomic) { m_meshLayer = atomic; }
    /// How many atomics the rig has drawn since it was made, with or without the mesh views: the play mode counts a
    /// human's passes from it for the Entity inspector's Mesh group.
    /// @coney-only the Entity inspector's Mesh group (docs/guides/debug-menu.md#mesh-group)
    [[nodiscard]] std::uint64_t atomicsDrawn() const { return m_atomicsDrawn; }

    /// This frame's camera (RenderWare's axes), as beginFrame() was given it.
    [[nodiscard]] const world::CameraPose& pose() const { return m_pose; }
    /// This frame's lighting frame, built by beginFrame() after the cull, with each atomic's selection added as it is
    /// drawn: what the lighting lab and the GPU renderer's shadow maps and per-pixel lights read
    /// (docs/engine/renderer.md#lighting). Nothing draws from it yet.
    [[nodiscard]] const graphics::LightingFrame& lightingFrame() const { return m_frame; }
    /// The light manager, for the summary.
    [[nodiscard]] const graphics::LightManager& lights() const { return m_lighting.lights; }
    /// The level's fog, which the world pass clears to and fogs with.
    [[nodiscard]] const graphics::WorldFog& fog() const { return m_lighting.fog; }

  private:
    // Puts the lights of `selection` that the lab's layers keep into the rig's world, mirrored from the manager's
    // records, and keeps `selection` for the frame's row of the atomic drawn next (renderInRig()).
    void useLights(const graphics::LightSelection& selection);
    // Draws `atomic` inside the rig's world (renderLit()), or through the mesh views' hook while one is set, as the
    // kind the caller's DrawKindScope names; first adds the lighting frame's row of its selection (recordSelection()).
    void renderInRig(rw::Atomic* atomic);
    // Draws `atomic` inside the rig's world, so librw lights it with the rig's lights, with its dual layer
    // (drawLitAtomic()), through the draw layer while m_routed is set (the world's atomics, the objects' and the cars',
    // items 7 and 8 of docs/engine/renderer.md#order), else on librw directly (the humans, which item 9 routes).
    void renderLit(rw::Atomic* atomic);
    // Adds the row of the selection useLights() kept to the lighting frame, for `atomic`: its caller from the draw
    // kind, its sphere and the inputs the draw functions pass (docs/research/lighting.md#select).
    void recordSelection(rw::Atomic* atomic);
    // The lights of `selection` that the lab's layers keep, by value, for `atomic`'s frame-description draw: keeps the
    // selection for the frame's row, counts the atomic as drawn and adds the row (recordSelection()).
    [[nodiscard]] std::vector<graphics::SceneLight> lightsOf(const graphics::LightSelection& selection,
                                                             rw::Atomic* atomic);
    // The light record `index` as the rig draws it: its type, the colour the lab's layers leave, its place.
    [[nodiscard]] graphics::SceneLight sceneLightOf(std::uint16_t index) const;
    // The librw light that mirrors record `index`, made on first use with the record's type.
    rw::Light* mirror(std::uint16_t index);

    graphics::LevelLighting& m_lighting;
    std::optional<graphics::SpriteSheet> m_coronaSheet;
    std::optional<graphics::SpriteSheet> m_shadowSheet;
    rw::World* m_world = nullptr;                           // owned: holds the current atomic's lights
    std::vector<rw::Light*> m_mirrors;                      // owned, with their frames; null where none is made yet
    std::vector<rw::Light*> m_inWorld;                      // the mirrors now in m_world
    world::CameraPose m_pose;                               // this frame's camera
    graphics::LightingFrame m_frame;                        // this frame's lighting frame (beginFrame())
    MeshViewHook* m_meshViews = nullptr;                    // setMeshViews(); null: none
    const rw::Atomic* m_meshLayer = nullptr;                // setMeshLayer()
    std::uint64_t m_atomicsDrawn = 0;                       // atomicsDrawn()
    const graphics::LightingSwitches* m_switches = nullptr; // setSwitches(); null: none
    std::optional<graphics::LightView> m_gameView;          // setGameView()
    graphics::LightingHold m_hold;                          // Hold lighting's moment
    std::optional<graphics::LightSelection> m_pending;      // useLights()'s selection, until its atomic is drawn
    bool m_background = false;                              // the atomic drawn next is the background's
    bool m_hiddenHuman = false;                             // noteHiddenHuman()
    bool m_routed = false;                                  // the atomic drawn next goes through the draw layer
};

} // namespace coney::platform
