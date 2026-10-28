// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity inspector's Mesh group over a play mode and the selection's normals and bone axes
// (docs/guides/debug-menu.md#mesh-views): which librw atomics an entity draws with, what its model is made of and what
// the last frame drew of it. Coney's own debug tooling, in the debug tools' target
// (docs/guides/building.md#debug-tools): read-only, nothing here changes how the game plays or draws.
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <rw.h>

#include "animation/skeleton.h"
#include "characters/character_rig.h"
#include "debug/mesh_stats.h"
#include "debug/mesh_views.h"
#include "platform/play_level_controls.h"

namespace coney::platform {

namespace {

// The texture a human's mesh is drawn with, as librw has it loaded: the first material's (every material shares it).
debug::TextureInfo textureOf(const CharacterMesh& mesh) {
    const rw::Geometry* geometry = mesh.atomic()->geometry;
    if (geometry->matList.numMaterials == 0 || geometry->matList.materials[0]->texture == nullptr) {
        return {};
    }
    const rw::Texture* texture = geometry->matList.materials[0]->texture;
    debug::TextureInfo info{.name = texture->name, .width = 0, .height = 0};
    if (texture->raster != nullptr) {
        info.width = texture->raster->width;
        info.height = texture->raster->height;
    }
    return info;
}

// The distance fade a human's mesh was last drawn at: PlayLighting::drawHuman() puts it in the materials' alpha.
float fadeOf(const CharacterMesh& mesh) {
    const rw::Geometry* geometry = mesh.atomic()->geometry;
    return geometry->matList.numMaterials == 0
               ? 1.0F
               : static_cast<float>(geometry->matList.materials[0]->color.alpha) / 255.0F;
}

// The pose bones' parents, for the bone axes' links.
std::vector<int> poseParents() {
    std::vector<int> parents(anim::kPoseBones);
    for (std::size_t bone = 0; bone < anim::kPoseBones; ++bone) {
        parents[bone] = anim::poseBoneParent(bone);
    }
    return parents;
}

} // namespace

std::optional<PlayLevelControls::HumanDraw> PlayLevelControls::humanDrawOf(const PlayLevelMode& mode,
                                                                           debug::EntityId id, float alpha,
                                                                           const human::PlayerSnapshot* snapshot) {
    if (id == kInspectPlayerId) {
        const human::PlayerSnapshot drawn =
            snapshot != nullptr ? *snapshot
                                : human::interpolate(mode.m_player->previous(), mode.m_player->current(), alpha);
        return HumanDraw{.mesh = mode.m_mesh.get(),
                         .character = &mode.playerCharacter(),
                         .pose = drawn.pose,
                         .feet = drawn.feet,
                         .heading = drawn.heading,
                         .lean = drawn.lean};
    }
    if (const auto index = inspectIndexIn(id, kInspectTargetBase); index && *index < mode.m_targets.size()) {
        const auto& target = mode.m_targets[*index];
        const human::TargetSnapshot drawn =
            human::interpolate(target.human->previous(), target.human->current(), alpha);
        return HumanDraw{.mesh = target.mesh.get(),
                         .character = target.character != nullptr ? target.character : mode.m_character.get(),
                         .pose = drawn.pose,
                         .feet = drawn.feet,
                         .heading = drawn.heading,
                         .lean = 0.0F};
    }
    if (const auto index = inspectIndexIn(id, kInspectAiBase);
        index && mode.m_ai && *index < mode.m_ai->humans().size() && *index < mode.m_fighterMeshes.size()) {
        const ai::AiHuman& fighter = mode.m_ai->humans()[*index];
        const auto& mesh = mode.m_fighterMeshes[*index];
        if (fighter.removed || mesh.hidden) {
            return std::nullopt;
        }
        const human::TargetSnapshot drawn = human::interpolate(fighter.previous, fighter.current, alpha);
        return HumanDraw{.mesh = mesh.mesh.get(),
                         .character = mesh.character,
                         .pose = drawn.pose,
                         .feet = drawn.feet,
                         .heading = drawn.heading,
                         .lean = 0.0F};
    }
    return std::nullopt;
}

std::vector<const rw::Atomic*> PlayLevelControls::atomicsOf(const PlayLevelMode& mode, debug::EntityId id) {
    const std::optional<HumanDraw> human = humanDrawOf(mode, id, 1.0F, nullptr);
    if (!human || human->mesh == nullptr) {
        return {};
    }
    return {human->mesh->atomic(), human->mesh->bloodAtomic()};
}

std::optional<debug::WatchGroup> PlayLevelControls::meshGroupOf(const PlayLevelMode& mode, debug::EntityId id) {
    const std::optional<HumanDraw> human = humanDrawOf(mode, id, 1.0F, nullptr);
    if (!human || human->mesh == nullptr || human->character == nullptr) {
        return std::nullopt;
    }
    const debug::MeshStats stats = debug::characterMeshStats(human->character->assets().model, textureOf(*human->mesh));
    // Where the mesh is skinned is the mesh's own answer: the vertex shader when the skin pipeline runs and the model
    // fits it, else the CPU (CharacterMesh::gpuSkinned()).
    const debug::MeshDrawn drawn{.passes = human->mesh->drawnPasses(),
                                 .fade = fadeOf(*human->mesh),
                                 .skinning = human->mesh->gpuSkinned() ? "GPU" : "CPU"};
    return debug::describeMesh(stats, drawn);
}

void PlayLevelControls::addMeshLines(const PlayLevelMode& mode, const debug::DebugDrawOptions& options,
                                     debug::DebugLines& lines, const human::PlayerSnapshot& snapshot, float alpha) {
    if ((!options.mesh.normals && !options.mesh.boneAxes) || options.inspected == debug::kNoEntity) {
        return;
    }
    const std::optional<HumanDraw> human = humanDrawOf(mode, options.inspected, alpha, &snapshot);
    if (!human || human->character == nullptr) {
        return;
    }
    // The pose's bones in the character's space, as the play mode skins it, then placed as it draws it.
    const auto bones = anim::boneTransforms(human->character->skeleton(), human->pose);
    const anim::Mat34 placement = debug::characterPlacement(human->feet, human->heading, human->lean);
    if (options.mesh.boneAxes) {
        const std::vector<int> parents = poseParents();
        debug::addBoneAxes(lines, bones, parents, placement);
    }
    if (options.mesh.normals) {
        // Skinned here for the selection alone, whichever way the mesh itself is skinned, so the normals follow the
        // animation on either path.
        const characters::CharacterModel& model = human->character->assets().model;
        const std::vector<anim::Mat34> matrices = characters::skinningMatrices(model, bones);
        std::vector<anim::Vec3> positions(model.vertices.size());
        std::vector<anim::Vec3> normals(model.vertices.size());
        characters::skinVertices(model, matrices, positions, normals);
        debug::addNormals(lines, positions, normals, placement);
    }
}

} // namespace coney::platform
