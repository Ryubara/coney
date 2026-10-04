// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_rig.h"

#include <cstddef>

#include "core/assert.h"

namespace coney::characters {

namespace {

// The weight from which a vertex counts as held by a bone in jointMismatch().
constexpr float kSignificantWeight = 0.2F;

// kClumpToPose's inverse, its transpose: x stays, y becomes -z, z becomes y.
constexpr anim::Mat34 kPoseToClump{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}, {}};

} // namespace

std::size_t poseBoneOfNode(const CharacterModel& model, std::size_t nodeIndex) {
    CONEY_ASSERT(nodeIndex < model.clump.hierarchy.size());
    return static_cast<std::size_t>(model.clump.hierarchy[nodeIndex].id) + 2;
}

anim::Skeleton characterSkeleton(const CharacterModel& model) {
    anim::Skeleton skeleton;
    skeleton.offsets = model.boneOffsets;
    skeleton.rootHeightOffset = model.rootHeightOffset;
    for (const ClumpFrame& frame : model.clump.frames) {
        if (!frame.hanimId) {
            continue;
        }
        // A frame's rotation relative to its parent, turned into the pose's axes: C * R * C^-1.
        anim::Mat34 rotation = frame.matrix;
        rotation.t = {};
        const auto bone = static_cast<std::size_t>(*frame.hanimId) + 2;
        if (bone < anim::kPoseBones) {
            skeleton.bindRotations[bone] =
                anim::quatFromMatrix(anim::multiply(anim::multiply(kClumpToPose, rotation), kPoseToClump));
        }
    }
    return skeleton;
}

std::vector<anim::Mat34> skinningMatrices(const CharacterModel& model,
                                          std::span<const anim::Mat34, anim::kPoseBones> bones) {
    std::vector<anim::Mat34> matrices;
    matrices.reserve(model.clump.skin.inverseBind.size());
    for (std::size_t node = 0; node < model.clump.skin.inverseBind.size(); ++node) {
        const anim::Mat34& bone = bones[poseBoneOfNode(model, node)];
        matrices.push_back(anim::multiply(anim::multiply(bone, kClumpToPose), model.clump.skin.inverseBind[node]));
    }
    return matrices;
}

void skinVertices(const CharacterModel& model, std::span<const anim::Mat34> matrices, std::span<anim::Vec3> positions,
                  std::span<anim::Vec3> normals) {
    CONEY_ASSERT(positions.size() == model.vertices.size() && normals.size() == model.vertices.size());
    for (std::size_t i = 0; i < model.vertices.size(); ++i) {
        const SkinVertex& vertex = model.vertices[i];
        anim::Vec3 position;
        anim::Vec3 normal;
        for (std::size_t w = 0; w < kVertexWeights; ++w) {
            if (vertex.weights[w] == 0.0F) {
                continue;
            }
            const anim::Mat34& matrix = matrices[vertex.bones[w]];
            position =
                anim::add(position, anim::scale(anim::transformPoint(matrix, vertex.position), vertex.weights[w]));
            normal = anim::add(normal, anim::scale(anim::transformDirection(matrix, vertex.normal), vertex.weights[w]));
        }
        positions[i] = position;
        normals[i] = anim::normalise(normal);
    }
}

float jointMismatch(const CharacterModel& model, std::span<const anim::Mat34> matrices) {
    double total = 0.0;
    std::size_t counted = 0;
    for (const SkinVertex& vertex : model.vertices) {
        // The vertex under its first two significant bones alone.
        std::array<anim::Vec3, 2> placed{};
        std::size_t found = 0;
        for (std::size_t w = 0; w < kVertexWeights && found < placed.size(); ++w) {
            if (vertex.weights[w] >= kSignificantWeight) {
                placed[found++] = anim::transformPoint(matrices[vertex.bones[w]], vertex.position);
            }
        }
        if (found == placed.size()) {
            total += anim::distance(placed[0], placed[1]);
            ++counted;
        }
    }
    return counted == 0 ? 0.0F : static_cast<float>(total / static_cast<double>(counted));
}

} // namespace coney::characters
