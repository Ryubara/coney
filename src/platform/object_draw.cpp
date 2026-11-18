// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the frame description's seam for the objects' and cars' draws (docs/engine/renderer.md#frame-description)
#include "platform/object_draw.h"

#include <array>
#include <vector>

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
    // The debug tools' mesh views decide per atomic what is drawn, and a mesh that did not convert has no id: both go
    // through the lighting rig as before, the materials changed for the draw.
    if (!draw.mesh.mesh.valid() || lighting.meshViewsOn()) {
        const MaterialChange colours(*draw.atomic, draw.material);
        if (draw.carPart) {
            lighting.drawCarAtomic(draw.atomic);
        } else {
            lighting.drawObjectAtomic(draw.atomic);
        }
        return;
    }
    // The frame description's draw: the meshes by id, where the atomic is placed, its colours, the states the pass
    // set and the lights the light manager selects for its sphere; the GL3 renderer changes the colours and draws it.
    const rw::Matrix* ltm = draw.atomic->getFrame()->getLTM();
    const auto axis = [](const rw::V3d& v) { return std::array<float, 3>{v.x, v.y, v.z}; };
    graphics::Draw3d scene;
    scene.mesh = draw.mesh.mesh;
    scene.dualLayer = draw.mesh.dualLayer;
    scene.transform = graphics::Transform3d{
        .right = axis(ltm->right), .up = axis(ltm->up), .at = axis(ltm->at), .position = axis(ltm->pos)};
    scene.material = draw.material;
    scene.states = sceneStatesInForce();
    const std::vector<graphics::SceneLight> lights =
        draw.carPart ? lighting.carLights(draw.atomic) : lighting.objectLights(draw.atomic);
    engine.drawScene(scene, lights);
}

} // namespace coney::platform
