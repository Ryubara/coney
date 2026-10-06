// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "characters/character_model.h"

// The platform-neutral half of Coney's reference renders of characters and objects (`coney --render-references`,
// docs/guides/building.md#reference-images): where the fixed camera stands for a model, how an object's model is
// turned to face it, how a supersampled frame is reduced to the image, and what the image file is called. Pure and
// deterministic, so the same disc gives byte-identical images on every run; the renderer in src/platform/ only loads,
// draws and reads back.

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

/// The rotation from an object model's axes to the reference pose's (z up, the front towards +y, as a character's):
/// x becomes -x, y becomes z, z becomes y. The models stand with y up and their front towards +z, so this stands
/// them up facing the camera. **Coney's choice**, checked on the rendered images
/// (docs/research/level-loading.md#the-object-list).
inline constexpr anim::Mat34 kObjectToPose{{-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, {}};

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

/// The transform that places an object's model for its reference image: the model's own frame (`modelFrame`, its
/// clump frames composed), then kObjectToPose.
[[nodiscard]] anim::Mat34 objectReferenceTransform(const anim::Mat34& modelFrame);

/// The lists a reference render can write, each into a folder of its own below the output folder.
enum class ReferenceList : std::uint8_t {
    Characters, ///< The Character List's records, by model name.
    Objects,    ///< The Object List's records, by object type name.
};

/// The folder a list's images go into, below the output folder: `characters` or `objects`, as
/// docs/references/index.md names them.
[[nodiscard]] std::string_view referenceFolder(ReferenceList list);

/// The name hash an `--only` request stands for: `0x` and hex digits as given, otherwise the CRC-32 of the name in
/// lower case (characterNameHash()), which is how both lists hash their names.
[[nodiscard]] std::uint32_t referenceRequestHash(std::string_view request);

/// The names in a name list's text, one per line: blank lines and lines starting with '#' are skipped, and spaces,
/// tabs and carriage returns around a name are trimmed.
[[nodiscard]] std::vector<std::string> parseNameList(std::string_view text);

/// The reference image's file name for a Character List or Object List record: its name when `name` is known (not
/// empty), otherwise the record's name hash as eight lower-case hex digits; with `.png`.
[[nodiscard]] std::string referenceFileName(std::uint32_t nameHash, std::string_view name);

} // namespace coney::characters
