// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the frame description's seam for the objects' and cars' draws (docs/engine/renderer.md#frame-description)
#include "platform/object_draw.h"

#include <rw.h>

#include "platform/lit_atomic.h"
#include "platform/render_engine.h"
#include "platform/scene_assets.h"
#include "platform/scene_lighting.h"
#include "platform/world_atomic.h"

namespace coney::platform {

ObjectMesh addObjectMesh(SceneAssets& assets, rw::Atomic& atomic) {
    ObjectMesh mesh;
    if (auto id = assets.addAtomic(atomic); id) {
        mesh.mesh = *id;
    }
    if (rw::Atomic* layer = dualLayerOf(&atomic); layer != nullptr) {
        if (auto id = assets.addAtomic(*layer); id) {
            mesh.dualLayer = *id;
        }
    }
    return mesh;
}

void removeObjectMesh(SceneAssets& assets, const ObjectMesh& mesh) {
    assets.removeMesh(mesh.mesh);
    assets.removeMesh(mesh.dualLayer);
}

void drawObject(RenderEngine& engine, SceneLighting& lighting, const ObjectDraw& draw) {
    (void)engine;
    // Stand-in until the frame description draws them: the materials changed, then the lighting rig.
    const MaterialChange colours(*draw.atomic, draw.material);
    lighting.drawObjectAtomic(draw.atomic);
}

} // namespace coney::platform
