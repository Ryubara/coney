// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <span>
#include <vector>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "animation/skeleton.h"
#include "characters/character_model.h"

// From a pose to a skinned character: the skeleton a model gives the animation player, the skinning matrices of its
// 32 skin bones, and the skinned vertices. Pure and platform-neutral; the renderer only uploads the result.
//
// The clump's skeleton (RenderWare frames, model space with x up) and the clips' skeleton (34 pose bones in game axes,
// z up) differ by one fixed rotation, Coney's kClumpToPose: a clip's local rotations and the bone offset chunk are the
// clump's frames turned by it (x, y, z) -> (x, -z, y). A skin bone's matrix is then
// `pose bone transform * kClumpToPose * inverse bind matrix`. Evidence (Coney's disc test, characters.md): with it,
// the bone offsets equal the frames' translations turned, and the vertices weighted to two bones land on the same
// point from both, which no other axis change gives.

namespace coney::characters {

/// The rotation from the clump's model space and bone frames to the pose's: x stays, y becomes z, z becomes -y.
inline constexpr anim::Mat34 kClumpToPose{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, -1.0F, 0.0F}, {}};

/// The pose bone of HAnim node `nodeIndex`: its bone id plus 2 (bones 0 and 1, the root and the pelvis, have no node).
[[nodiscard]] std::size_t poseBoneOfNode(const CharacterModel& model, std::size_t nodeIndex);

/// The skeleton the animation player uses for `model`: offsets from the bone offset chunk; rest rotations, for bones a
/// clip does not animate, from the clump's frames turned into the pose's axes (identity for the root and the pelvis,
/// which have no frame of their own: **Coney's choice**, the pages do not say where the original's rest pose comes
/// from).
[[nodiscard]] anim::Skeleton characterSkeleton(const CharacterModel& model);

/// The skinning matrix of each skin bone (HAnim node index) for the pose bones' transforms `bones`, mapping a vertex
/// from the clump's model space to the character's space.
[[nodiscard]] std::vector<anim::Mat34> skinningMatrices(const CharacterModel& model,
                                                        std::span<const anim::Mat34, anim::kPoseBones> bones);

/// Skins every vertex of `model`: each position and normal blended over its bones' matrices by its weights.
/// `positions` and `normals` must hold one entry per vertex (checked by CONEY_ASSERT).
void skinVertices(const CharacterModel& model, std::span<const anim::Mat34> matrices, std::span<anim::Vec3> positions,
                  std::span<anim::Vec3> normals);

/// How far apart a vertex lands when skinned by each of its bones alone, averaged over the vertices with two or more
/// weights of at least 0.2: a few centimetres when the rig is right, much more when bones and skin disagree. Coney's
/// check of kClumpToPose; 0 when no vertex qualifies.
[[nodiscard]] float jointMismatch(const CharacterModel& model, std::span<const anim::Mat34> matrices);

} // namespace coney::characters
