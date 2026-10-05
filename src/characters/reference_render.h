// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "characters/character_model.h"

// The platform-neutral half of Coney's character reference renders (`coney --render-references`,
// docs/guides/building.md#character-reference-images): where the fixed camera stands for a model, how a supersampled
// frame is reduced to the image, and what the image file is called. Pure and deterministic, so the same disc gives
// byte-identical images on every run; the renderer in src/platform/ only draws and reads back.

namespace coney::characters {

/// **Coney's choice** for the reference camera: three-quarter front. The character faces +y in the pose's axes (z up,
/// characters.md#character-geometry); the camera stands this far round from straight in front, towards the
/// character's right (+x), and this far above the horizontal, looking at the model's middle.
inline constexpr float kReferenceYaw = 0.6108652F;   // 35 degrees
inline constexpr float kReferencePitch = 0.1745329F; // 10 degrees
/// Half the view's opening at unit distance (tan of half the field of view, about 28 degrees in all) and the share
/// of the frame left empty on each side around the model.
inline constexpr float kReferenceHalfView = 0.25F;
inline constexpr float kReferenceMargin = 0.06F;

/// Where the reference camera stands: its position, the point it looks at, and its axes (`right` is forward × up, as
/// OrbitPose).
struct ReferenceView {
    anim::Vec3 position;
    anim::Vec3 target;
    anim::Vec3 forward;
    anim::Vec3 up;
    anim::Vec3 right;
};

/// The model's bind pose in the pose's axes: every vertex position and normal turned from the clump's model space by
/// kClumpToPose (character_rig.h). This is the skinned result of the bind skeleton, without sampling any clip.
void bindPoseVertices(const CharacterModel& model, std::span<anim::Vec3> positions, std::span<anim::Vec3> normals);

/// The fixed three-quarter camera framed on `points`: looking along the direction kReferenceYaw and kReferencePitch
/// give, at the middle of the points' projected extent, from the nearest distance at which every point lies inside a
/// square view of half-size kReferenceHalfView less the margin. A camera on the origin's +y side when `points` is
/// empty. Deterministic: the same points give the same view.
[[nodiscard]] ReferenceView frameReference(std::span<const anim::Vec3> points);

/// Reduces a square RGBA image of `size` × `size` pixels (rows top down) by `factor` in each direction, averaging
/// each block with its alpha as the weight (premultiplied), so a transparent background leaves no colour fringe.
/// `size` must be a multiple of `factor` (checked by CONEY_ASSERT).
[[nodiscard]] std::vector<std::uint8_t> downsampleRgba(std::span<const std::uint8_t> rgba, int size, int factor);

/// The reference image's file name for a Character List record: its model name when `name` is known (not empty),
/// otherwise the record's name hash as eight lower-case hex digits; with `.png`.
[[nodiscard]] std::string referenceFileName(std::uint32_t nameHash, std::string_view name);

} // namespace coney::characters
