// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

#include "platform/perf_probes.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
} // namespace rw

namespace coney::platform {

class PlayLevelMode;

/// The hook through which the debug tools' mesh views (docs/guides/debug-menu.md#mesh-views) draw the play mode's
/// models: the game offers it, the debug tools implement it (platform/mesh_views.h), and the game links nothing of
/// theirs (docs/guides/building.md#debug-tools). While it says it is off for a frame, or none is set, the scene draws
/// exactly as without it: SceneLighting does not call it at all.
class MeshViewHook {
  public:
    virtual ~MeshViewHook() = default;

    /// The start of a frame `mode` draws; false when every view is off this frame, and then draw() is not called.
    virtual bool beginFrame(const PlayLevelMode& mode) = 0;
    /// Draws `atomic`, a model of `kind` (what the play mode's DrawKindScope around the draw names), as the views say.
    /// `shaded` makes the game's own draw of it, lit; the hook calls it, or not, and may draw the same atomic again in
    /// other passes (flat, wire) through its own pipeline, so the passes follow its skinning whether it is done on the
    /// CPU or in the shader. `layer` marks a second pass of a model already handed in (a human's blood layer, as
    /// SceneLighting::setMeshLayer() marks it): it is drawn only with the shaded draw.
    virtual void draw(rw::Atomic* atomic, DrawKind kind, bool layer, const std::function<void()>& shaded) = 0;
};

} // namespace coney::platform
