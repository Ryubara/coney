// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "debug/debug_session.h"
#include "debug/mesh_views.h"
#include "debug/play_controls.h"
#include "platform/mesh_view_hook.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Light;
struct World;
} // namespace rw

namespace coney::platform {

/// The Debug draw page's mesh views drawn (docs/guides/debug-menu.md#mesh-views): the play mode's hook
/// (MeshViewHook), over the session's Debug draw options. Each model the scene draws gets the passes
/// debug::meshPlanFor() gives it: the game's own shaded draw, a flat unlit fill and a wireframe, each a draw of the
/// model's own librw atomic through its own pipeline, so what is drawn is the posed mesh however it is skinned (on the
/// CPU into the vertices, or in the vertex shader). The flat and wire passes swap the atomic's material colours and
/// textures for the draw, light it with one white ambient light and no fog, and put them back; the wire is OpenGL's
/// line polygon mode, pulled a little towards the camera so it sits on the fill.
///
/// Debug tooling in the debug tools' target (docs/guides/building.md#debug-tools). While every view is off,
/// beginFrame() says so and the scene draws as it always does. Needs a running RenderEngine while on; makes its librw
/// world and light at its first flat or wire pass and destroys them with itself, which must be before the engine stops.
class MeshViews final : public MeshViewHook {
  public:
    /// Views as `options` switch them; `options` must outlive this.
    explicit MeshViews(const debug::DebugDrawOptions& options) : m_options(options) {}
    ~MeshViews() override;
    MeshViews(const MeshViews&) = delete;
    MeshViews& operator=(const MeshViews&) = delete;
    MeshViews(MeshViews&&) = delete;
    MeshViews& operator=(MeshViews&&) = delete;

    /// Whether any view is on, and if so the selection's atomics in `mode` for this frame.
    bool beginFrame(const PlayLevelMode& mode) override;
    void draw(rw::Atomic* atomic, DrawKind kind, bool layer, const std::function<void()>& shaded) override;

    /// The materials the flat (filled) passes have drawn so far this frame: each is a draw, and the wire passes add
    /// none (for the log of a run, and the tests).
    [[nodiscard]] std::size_t passDraws() const { return m_drawIndex; }

  private:
    // Draws `atomic` flat and unlit, each material coloured as `plan` says, as lines when `lines`.
    void flatPass(rw::Atomic* atomic, const debug::MeshDrawPlan& plan, bool lines);
    // The white ambient light's world, made on first use.
    rw::World* flatWorld();

    const debug::DebugDrawOptions& m_options;
    std::vector<const rw::Atomic*> m_selected; // this frame's selection
    std::size_t m_drawIndex = 0;               // the flat passes' draws this frame, for the draw-call colours
    rw::World* m_world = nullptr;              // owned, with m_light
    rw::Light* m_light = nullptr;              // owned, with its frame
};

/// `--mesh-view` (docs/guides/running-options.md): the views of a spec switched on at the first frame the debug menus
/// have a play mode with entities, the entity nearest the player selected when the spec asks, and, after the spec's
/// frames, every view switched off again and that selection dropped, so a run's last frame can be checked against
/// the same run without the option. Debug tooling.
class MeshViewRun {
  public:
    explicit MeshViewRun(debug::MeshViewSpec spec) : m_spec(spec) {}

    /// Called at the start of every frame, before its steps: applies the spec to `session`'s Debug draw options when
    /// it can, and counts its frames.
    void frame(debug::DebugSession& session);

  private:
    debug::MeshViewSpec m_spec;
    bool m_applied = false;
    bool m_done = false;
    int m_frames = 0;
};

} // namespace coney::platform
