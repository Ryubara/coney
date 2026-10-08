// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/anim_clip.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <memory>
#include <utility>

#include "fileio/reader.h"

namespace coney::anim {

namespace {

// Descriptor offsets (docs/research/formats/animation.md#descriptor-chunk-0x02-80-bytes).
constexpr std::size_t kDisplacementOffset = 0x04;
constexpr std::size_t kSectionSizesOffset = 0x10;
constexpr std::size_t kMaskOffset = 0x20;
constexpr std::size_t kMaskBytes = 5;
constexpr std::size_t kNameOffset = 0x25;
constexpr std::size_t kNameBytes = 30;
constexpr std::size_t kFlagsOffset = 0x44;

// The scales of the stored integers: positions x / 1023, y / 1023, z / 2047; rotations x, y, z × 2^-15.
constexpr float kPositionScaleXy = 1.0F / 1023.0F;
constexpr float kPositionScaleZ = 1.0F / 2047.0F;
constexpr float kRotationScale = 1.0F / 32768.0F;

// The signed 16-bit value at `at` of `bytes`, which the caller has checked holds it.
std::int16_t s16At(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::int16_t>(std::to_integer<std::uint16_t>(bytes[at]) |
                                     static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[at + 1]) << 8U));
}

// The unsigned 32-bit little-endian value at `at` of `bytes`, which the caller has checked holds it.
std::uint32_t u32At(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[at + i]) << (8U * i);
    }
    return value;
}

// A position from three stored integers.
Vec3 decodePosition(std::int16_t x, std::int16_t y, std::int16_t z) {
    return Vec3{static_cast<float>(x) * kPositionScaleXy, static_cast<float>(y) * kPositionScaleXy,
                static_cast<float>(z) * kPositionScaleZ};
}

// A rotation from three stored integers; w is the non-negative root that makes it a unit quaternion.
Quat decodeRotation(std::int16_t x, std::int16_t y, std::int16_t z) {
    const float qx = static_cast<float>(x) * kRotationScale;
    const float qy = static_cast<float>(y) * kRotationScale;
    const float qz = static_cast<float>(z) * kRotationScale;
    const float rest = 1.0F - qx * qx - qy * qy - qz * qz;
    return Quat{qx, qy, qz, rest > 0.0F ? std::sqrt(rest) : 0.0F};
}

// One raw key: frames since the previous key and the three stored integers (byte +1 is not read, as in the original).
struct RawKey {
    std::uint8_t delta = 0;
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
};

// Splits a section into channels: a key with delta 0 starts a channel, and the section must start with one. `name`
// is the section's name for messages.
std::expected<std::vector<std::vector<RawKey>>, Error> splitChannels(std::span<const std::byte> section,
                                                                     std::string_view name) {
    if (section.size() % kClipKeyBytes != 0) {
        return fail(ErrorCode::Invalid,
                    std::format("section {} holds {} bytes, not a whole number of keys", name, section.size()));
    }
    std::vector<std::vector<RawKey>> channels;
    for (std::size_t at = 0; at < section.size(); at += kClipKeyBytes) {
        const RawKey key{std::to_integer<std::uint8_t>(section[at]), s16At(section, at + 2), s16At(section, at + 4),
                         s16At(section, at + 6)};
        if (key.delta == 0) {
            channels.emplace_back();
        } else if (channels.empty()) {
            return fail(ErrorCode::Invalid, std::format("section {} does not start with a channel's first key", name));
        }
        channels.back().push_back(key);
    }
    return channels;
}

// Turns a position channel's raw keys into keys with absolute frames.
std::vector<PositionKey> positionKeys(const std::vector<RawKey>& raw) {
    std::vector<PositionKey> keys;
    keys.reserve(raw.size());
    std::uint32_t frame = 0;
    for (const RawKey& key : raw) {
        frame += key.delta;
        keys.push_back(PositionKey{frame, decodePosition(key.x, key.y, key.z)});
    }
    return keys;
}

// Turns a rotation channel's raw keys into keys with absolute frames.
std::vector<RotationKey> rotationKeys(const std::vector<RawKey>& raw) {
    std::vector<RotationKey> keys;
    keys.reserve(raw.size());
    std::uint32_t frame = 0;
    for (const RawKey& key : raw) {
        frame += key.delta;
        keys.push_back(RotationKey{frame, decodeRotation(key.x, key.y, key.z)});
    }
    return keys;
}

// Decodes a position section (A or B), which holds no channel or one.
std::expected<std::vector<PositionKey>, Error> positionSection(std::span<const std::byte> section,
                                                               std::string_view name) {
    auto channels = splitChannels(section, name);
    if (!channels) {
        return std::unexpected(std::move(channels.error()));
    }
    if (channels->size() > 1) {
        return fail(ErrorCode::Invalid, std::format("section {} holds {} channels, not one", name, channels->size()));
    }
    return channels->empty() ? std::vector<PositionKey>{} : positionKeys(channels->front());
}

// Reads the events after the sections.
std::vector<ClipEvent> readEvents(std::span<const std::byte> bytes, std::size_t count) {
    std::vector<ClipEvent> events;
    events.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::span<const std::byte> e = bytes.subspan(i * kClipEventBytes, kClipEventBytes);
        ClipEvent event;
        event.frame = static_cast<std::uint16_t>(s16At(e, 0));
        event.type = static_cast<std::uint16_t>(s16At(e, 2));
        event.value = s16At(e, 4);
        event.argument = u32At(e, 8);
        event.word = static_cast<std::uint16_t>(s16At(e, 6));
        event.position = decodePosition(s16At(e, 8), s16At(e, 10), s16At(e, 12));
        event.rotation = decodeRotation(s16At(e, 14), s16At(e, 16), s16At(e, 18));
        events.push_back(event);
    }
    return events;
}

} // namespace

const std::vector<RotationKey>* AnimClip::rotationChannel(std::size_t bone) const {
    const auto found = std::ranges::find(rotationBones, bone);
    if (found == rotationBones.end()) {
        return nullptr;
    }
    return &rotations[static_cast<std::size_t>(found - rotationBones.begin())];
}

std::uint32_t AnimClip::lastKeyFrame() const {
    std::uint32_t last = 0;
    if (!rootVelocity.empty()) {
        last = std::max(last, rootVelocity.back().frame);
    }
    if (!rootTranslation.empty()) {
        last = std::max(last, rootTranslation.back().frame);
    }
    for (const std::vector<RotationKey>& channel : rotations) {
        last = std::max(last, channel.back().frame);
    }
    return last;
}

std::expected<AnimClip, Error> parseAnimClip(std::span<const std::byte> descriptor,
                                             std::span<const std::byte> keyframes) {
    if (descriptor.size() != kClipDescriptorBytes) {
        return fail(ErrorCode::Invalid,
                    std::format("a clip descriptor holds {} bytes, not {}", descriptor.size(), kClipDescriptorBytes));
    }
    // The descriptor: the size check above covers every read.
    io::Reader reader(descriptor.subspan(kDisplacementOffset));
    AnimClip clip;
    clip.displacement.x = reader.readF32Le().value();
    clip.displacement.y = reader.readF32Le().value();
    clip.duration = reader.readF32Le().value();
    reader = io::Reader(descriptor.subspan(kSectionSizesOffset));
    const std::size_t sizeA = reader.readU16Le().value();
    const std::size_t sizeB = reader.readU16Le().value();
    const std::size_t sizeC = reader.readU32Le().value();
    const std::size_t channelCount = reader.readU16Le().value();
    const std::size_t eventCount = reader.readU16Le().value();
    for (std::size_t i = 0; i < kMaskBytes; ++i) {
        clip.boneMask |= std::uint64_t{std::to_integer<std::uint8_t>(descriptor[kMaskOffset + i])} << (8 * i);
    }
    for (std::size_t i = 0; i < sizeof(clip.flags); ++i) {
        clip.flags |= std::to_integer<std::uint32_t>(descriptor[kFlagsOffset + i]) << (8 * i);
    }
    const auto name = descriptor.subspan(kNameOffset, kNameBytes);
    for (const std::byte c : name) {
        if (c == std::byte{0}) {
            break;
        }
        clip.name.push_back(static_cast<char>(c));
    }
    if (!std::isfinite(clip.duration) || clip.duration < 0.0F) {
        return fail(ErrorCode::Invalid, std::format("clip {}: duration {} is not a time", clip.name, clip.duration));
    }
    if (clip.boneMask >> kPoseBones != 0) {
        return fail(ErrorCode::Invalid,
                    std::format("clip {}: its bone mask names a bone above {}", clip.name, kPoseBones - 1));
    }
    const auto maskBits = static_cast<std::size_t>(std::popcount(clip.boneMask));
    if (maskBits != channelCount) {
        return fail(ErrorCode::Invalid, std::format("clip {}: {} rotation channels, but {} bits in its mask", clip.name,
                                                    channelCount, maskBits));
    }

    // The keyframes: A, B, C, then the events, back to back.
    const std::size_t sections = sizeA + sizeB + sizeC;
    if (sections > keyframes.size() || eventCount * kClipEventBytes > keyframes.size() - sections) {
        return fail(ErrorCode::Truncated,
                    std::format("clip {}: sections of {} bytes and {} events do not fit in {} bytes of keyframes",
                                clip.name, sections, eventCount, keyframes.size()));
    }
    auto velocity = positionSection(keyframes.first(sizeA), "A");
    auto translation = positionSection(keyframes.subspan(sizeA, sizeB), "B");
    auto rotations = splitChannels(keyframes.subspan(sizeA + sizeB, sizeC), "C");
    if (!velocity || !translation || !rotations) {
        const Error& error = !velocity ? velocity.error() : !translation ? translation.error() : rotations.error();
        return fail(error.code, std::format("clip {}: {}", clip.name, error.message));
    }
    if (rotations->size() != channelCount) {
        return fail(ErrorCode::Invalid, std::format("clip {}: section C holds {} channels, its descriptor says {}",
                                                    clip.name, rotations->size(), channelCount));
    }
    clip.rootVelocity = std::move(*velocity);
    clip.rootTranslation = std::move(*translation);
    for (std::size_t bone = 0; bone < kPoseBones; ++bone) {
        if ((clip.boneMask >> bone & 1U) != 0) {
            clip.rotationBones.push_back(static_cast<std::uint8_t>(bone));
            clip.rotations.push_back(rotationKeys((*rotations)[clip.rotations.size()]));
        }
    }
    clip.events = readEvents(keyframes.subspan(sections), eventCount);
    return clip;
}

std::expected<void, Error> onAnimDataLoaded(chunk::ChunkStacks& stacks, std::uint32_t type) {
    auto descriptor = stacks.popChunk(type);
    if (!descriptor) {
        return std::unexpected(std::move(descriptor.error()));
    }
    auto keyframes = stacks.popChunk(kAnimKeyframesChunk);
    if (!keyframes) {
        return std::unexpected(std::move(keyframes.error()));
    }
    auto clip = parseAnimClip(descriptor->bytes, keyframes->bytes);
    if (!clip) {
        return std::unexpected(std::move(clip.error()));
    }
    stacks.pushObject(std::make_unique<AnimClipObject>(std::move(*clip)));
    return {};
}

} // namespace coney::anim
