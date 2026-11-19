// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "graphics/asset_store.h"
#include "graphics/frame_description.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
} // namespace rw

namespace coney::platform {

class RenderEngine;
class SceneAssets;
class SceneLighting;

/// A model atomic's meshes in the scene assets: the atomic and its dual layer (docs/research/rendering.md#dual), each
/// invalid where it is not held (no dual layer, or a mesh that did not convert).
struct ObjectMesh {
    graphics::MeshId mesh;
    graphics::MeshId dualLayer;
};

/// Converts `atomic` and its dual layer into `assets` (SceneAssets::addAtomic()), after its materials have their
/// textures; a mesh that does not convert keeps no id, and its draws go to librw as before (drawObject()).
[[nodiscard]] ObjectMesh addObjectMesh(SceneAssets& assets, rw::Atomic& atomic);
/// Drops `mesh`'s ids from `assets`.
void removeObjectMesh(SceneAssets& assets, const ObjectMesh& mesh);

/// One draw of a world object's atomic or a car part's: the atomic, placed at its frame for the draw, its meshes in
/// the scene assets, and how the draw changes its materials' colours (an object's tint, a part's paint).
struct ObjectDraw {
    rw::Atomic* atomic = nullptr;
    ObjectMesh mesh;
    graphics::Material3d material;
    /// A car part: its lights are selected at squared distance 0, as CarInstance_Render selects them
    /// (SceneLighting::drawCarAtomic()); a world object's for its squared distance to the camera.
    bool carPart = false;
};

/// Draws `draw` with the objects' lights for its squared distance to the camera, or at squared distance 0 for a car
/// part, and the point lights its sphere overlaps (SceneLighting::drawObjectAtomic()'s or drawCarAtomic()'s
/// selection), in the states in force (sceneStatesInForce()): onto the
/// frame description (RenderEngine::drawScene(), docs/engine/renderer.md#frame-description), or, while the debug
/// tools' mesh views are on or its mesh is not held, through `lighting`'s rig with its materials changed for the draw.
/// The world objects and the parked cars both draw this way (item 8 of docs/engine/renderer.md#order).
/// @coney-only the frame description's seam for the objects' and cars' draws; the original's ObjectRender_Draw and
/// CarInstance_Render draw through RenderWare (PlacedObjects::draw, ParkedCars::draw)
void drawObject(RenderEngine& engine, SceneLighting& lighting, const ObjectDraw& draw);

} // namespace coney::platform
