// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
} // namespace rw

namespace coney::platform {

/// Characters skinned on the GPU: a librw GL3 object pipeline of Coney's own whose vertex shader blends each vertex by
/// up to four bones, so a character's vertices are uploaded once and only its bone matrices change per frame
/// (docs/guides/performance.md#gpu-skinning). The lighting, fog, texture levels and alpha test are those of Coney's
/// default pipeline (platform/texture_lod.h), so a skinned character is drawn as the CPU-skinned one was. Coney's own:
/// the original skins on the PS2's vector unit, which has no counterpart here.

/// The most bones a skinned character may have: the shader's matrix array.
inline constexpr std::size_t kMaxSkinBones = 64;

/// Floats per bone matrix: a 4 x 4 matrix, column-major, as OpenGL takes it.
inline constexpr std::size_t kBoneFloats = 16;

/// The bone matrices one skinned character is drawn with: kMaxSkinBones column-major 4 x 4 matrices.
using BonePalette = std::array<float, kMaxSkinBones * kBoneFloats>;

/// Each vertex's bones and weights, as the shader reads them. Kept by the mesh while its atomics live.
struct SkinVertexData {
    std::vector<std::array<float, 4>> weights;        ///< 0 for an unused slot.
    std::vector<std::array<std::uint8_t, 4>> indices; ///< Into the bone palette.
};

/// Registers the plugins the skinned pipeline keeps its data in (a geometry's vertex weights, an atomic's palette) and
/// the shader's bone uniform. Between librw's Engine::init and Engine::open, with the other plugins.
void attachCharacterSkin();

/// Makes the skinned pipeline's shaders and the pipeline, once the GL3 device runs. False (and CPU skinning stays in
/// use) where the shaders cannot be made: OpenGL 2 or ES 2 (no texture levels), or a driver that refuses them.
bool startCharacterSkin();

/// Destroys the shaders and the pipeline, before the device stops.
void stopCharacterSkin();

/// Whether the skinned pipeline is running (startCharacterSkin() succeeded) and allowed.
[[nodiscard]] bool characterSkinOn();

/// Allows or forbids GPU skinning for meshes made from now on (`--cpu-skinning` forbids it, to compare the two).
void allowCharacterSkin(bool allowed);

/// Puts `atomic` on the skinned pipeline, its geometry's weights from `vertices` and its bones from `palette`; both
/// must outlive the atomic, and `vertices` must hold one entry per geometry vertex. Needs characterSkinOn().
void useCharacterSkin(rw::Atomic* atomic, const SkinVertexData& vertices, const BonePalette& palette);

} // namespace coney::platform
