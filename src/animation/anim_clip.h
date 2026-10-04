// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/chunk_stacks.h"
#include "core/error.h"

// A character animation clip in the game's own format: an 80-byte descriptor (chunk 0x02) and its keyframes (chunk
// 0x00), decoded at load into channels of keys with absolute start frames, so that sampling is a search per channel.
// Format: docs/research/formats/animation.md.

namespace coney::anim {

/// Bones of the game's character skeleton, in a clip and in a pose: 0 is the root, 1 the pelvis, then the 32 bones
/// of the model's HAnim hierarchy (docs/research/formats/animation.md#the-pose).
inline constexpr std::size_t kPoseBones = 34;

/// Frames a second the clips are keyed and played at.
inline constexpr float kClipFrameRate = 30.0F;

/// Bytes of a clip's descriptor (chunk 0x02).
inline constexpr std::size_t kClipDescriptorBytes = 80;

/// Bytes of one key and of one event in the keyframes.
inline constexpr std::size_t kClipKeyBytes = 8;
inline constexpr std::size_t kClipEventBytes = 24;

/// The chunk types of a clip: its keyframes (Anim Rot Keyframes) and its descriptor (Anim Data).
inline constexpr std::uint32_t kAnimKeyframesChunk = 0x00;
inline constexpr std::uint32_t kAnimDataChunk = 0x02;

/// A position key, decoded: from `frame` on, until the next key, the channel moves towards the next key's value.
struct PositionKey {
    std::uint32_t frame = 0; ///< Frames from the clip's start.
    Vec3 value;              ///< Metres: x / 1023, y / 1023, z / 2047 of the stored integers.
};

/// A rotation key, decoded: `w` rebuilt from `x, y, z` with a non-negative square root.
struct RotationKey {
    std::uint32_t frame = 0;
    Quat value;
};

/// One event of a clip, as stored; what the types trigger is not known yet (animation.md, open questions).
struct ClipEvent {
    std::uint16_t frame = 0;
    std::uint16_t type = 0;
    std::uint16_t word = 0; ///< The u16 at +6.
    Vec3 position;          ///< +8: scaled as a position key.
    Quat rotation;          ///< +0xe: x, y, z, with w rebuilt as a rotation key's.
};

/// A clip, decoded.
struct AnimClip {
    std::string name;                         ///< The descriptor's name, cut to 30 characters by the tools.
    Vec3 displacement;                        ///< The root's displacement over the whole clip in x and y (z is 0).
    float duration = 0.0F;                    ///< Seconds.
    std::uint64_t boneMask{};                 ///< Bit b set when bone b (0-33) has a rotation channel.
    std::vector<PositionKey> rootVelocity;    ///< Section A: empty when the clip has none.
    std::vector<PositionKey> rootTranslation; ///< Section B, the pelvis: empty when the clip has none.
    std::vector<std::vector<RotationKey>> rotations; ///< Section C: one channel per bit of the mask, in bone order.
    std::vector<std::uint8_t> rotationBones;         ///< The bone of each channel of `rotations`.
    std::vector<ClipEvent> events;

    /// The channel of `bone`'s rotation, or nullptr when the clip does not animate it.
    [[nodiscard]] const std::vector<RotationKey>* rotationChannel(std::size_t bone) const;
    /// The frame of the last key of any channel.
    [[nodiscard]] std::uint32_t lastKeyFrame() const;
};

/// Decodes a clip from its descriptor (exactly 80 bytes) and its keyframes. The keyframes are sections A (root
/// velocity), B (root translation) and C (rotations), sized by the descriptor, then the events; each section is split
/// into channels at every key whose frame delta is 0. Fails with ErrorCode::Invalid for a descriptor of another size,
/// a section that is not a whole number of keys or does not start a channel, more than one channel in A or B, a
/// rotation channel count other than the mask's bits or the descriptor's count, a mask bit above bone 33, or a duration
/// that is negative or not a number; ErrorCode::Truncated when the sections and events do not fit in the keyframes.
/// A rotation key whose x, y, z square to more than 1 (rounding) gets w = 0. Bytes after the events are allowed: the
/// chunks are padded to 16 bytes.
///
/// Research: docs/research/formats/animation.md#data
/// @orig 0x001041f8 AnimCursor_Init (unknown)
[[nodiscard]] std::expected<AnimClip, Error> parseAnimClip(std::span<const std::byte> descriptor,
                                                           std::span<const std::byte> keyframes);

/// A clip on the chunk system's object stack, as the Anim Data handler pushes it.
class AnimClipObject final : public chunk::LoadedObject {
  public:
    explicit AnimClipObject(AnimClip clip) : m_clip(std::move(clip)) {}

    [[nodiscard]] std::string_view describe() const override { return "animation"; }

    /// The clip.
    [[nodiscard]] AnimClip& clip() { return m_clip; }

  private:
    AnimClip m_clip;
};

/// The `onLoaded` handler of chunk type 0x02 (Anim Data), which the original's animation system registers at run
/// time: pops the descriptor (0x02) and the keyframes before it (0x00), decodes the clip and pushes it as an
/// AnimClipObject. Fails as ChunkStacks::popChunk() and parseAnimClip() do; the message names the clip when it can.
///
/// Research: docs/research/chunk-system.md#chunk-type-table
/// @orig 0x001045e0 AnimData_OnLoaded (unknown)
[[nodiscard]] std::expected<void, Error> onAnimDataLoaded(chunk::ChunkStacks& stacks, std::uint32_t type);

} // namespace coney::anim
