// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "graphics/asset_store.h"
#include "graphics/render_device.h"
#include "platform/perf_probes.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
} // namespace rw

namespace coney::platform {

class PlayLevelMode;

/// One material of a recorded model draw as the mesh views colour it: its texture's name (empty for none) and the alpha
/// the draw gives it (a human's distance fade, a sector's fade-in).
struct ViewMaterial {
    std::string_view texture;
    std::uint8_t alpha = 255;
};

/// The passes the mesh views give one recorded model draw (MeshViewHook::passesFor()): the game's own draw, a flat fill
/// and a wireframe, as the hook's draw() gives an atomic on the GL3 renderer.
struct ViewPasses {
    bool shaded = true;                 ///< The game's own draw, with its second layers (blood, dual).
    std::vector<graphics::Rgba> flat;   ///< A flat unlit fill, one colour for each material in order; empty: none.
    std::optional<graphics::Rgba> wire; ///< The triangles' edges in this colour over whatever is drawn; none: none.
};

/// The hook through which the debug tools' mesh views (docs/guides/debug-menu.md#mesh-views) draw the play mode's
/// models: the game offers it, the debug tools implement it (platform/mesh_views.h), and the game links nothing of
/// theirs (docs/guides/building.md#debug-tools). While it says it is off for a frame, or none is set, the scene draws
/// exactly as without it: SceneLighting does not call it at all.
class MeshViewHook {
  public:
    virtual ~MeshViewHook() = default;

    /// The start of a frame `mode` draws; false when every view is off this frame, and then draw() is not called.
    virtual bool beginFrame(const PlayLevelMode& mode) = 0;
    /// Whether the hook draws passes of its own over the models this frame (a wireframe or a mesh view other than
    /// shaded), as against only leaving kinds out (hides()). It does not decide the draw path: a renderer that draws
    /// the passes through librw (GL3) sends every model through the lighting rig for the hook whenever one is set
    /// (SceneLighting::meshViewsOn()), and one that records its draws (the GPU renderer) leaves the rig's passes off, a
    /// hidden kind out of its recording (RenderEngine::hideKindsOf()) and asks passesFor() for each recorded model. The
    /// default is true: a hook decides for every model unless it says otherwise.
    [[nodiscard]] virtual bool hasPasses() const { return true; }
    /// Whether the hook draws no model of `kind` this frame (the World page's parts switched off). A recorded draw of
    /// a model of a hidden kind is left out of the frame description, as draw() leaves it out of the rig's.
    [[nodiscard]] virtual bool hides(DrawKind /*kind*/) const { return false; }
    /// Draws `atomic`, a model of `kind` (what the play mode's DrawKindScope around the draw names), as the views say.
    /// `shaded` makes the game's own draw of it, lit; the hook calls it, or not, and may draw the same atomic again in
    /// other passes (flat, wire) through its own pipeline, so the passes follow its skinning whether it is done on the
    /// CPU or in the shader. `layer` marks a second pass of a model already handed in (a human's blood layer, as
    /// SceneLighting::setMeshLayer() marks it): it is drawn only with the shaded draw.
    virtual void draw(rw::Atomic* atomic, DrawKind kind, bool layer, const std::function<void()>& shaded) = 0;
    /// The passes the model `mesh` of `kind`, about to be recorded into the frame description, gets this frame: the
    /// same decision draw() makes for its atomic (which models a view applies to, the selection by the mesh ids of its
    /// models, the colours). `materials` are the mesh's, in order, as this draw colours them. Called by a renderer that
    /// records its draws (RenderEngine::drawScene()), once per model draw and never for a kind the hook hides; the
    /// default is the game's own draw alone, so a hook that only hides kinds changes nothing here.
    [[nodiscard]] virtual ViewPasses passesFor(DrawKind /*kind*/, graphics::MeshId /*mesh*/,
                                               std::span<const ViewMaterial> /*materials*/) {
        return {};
    }
};

} // namespace coney::platform
