// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenes/scene_record.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <utility>

#include "core/assert.h"

namespace coney::scenes {

namespace {

// The rotation keys' fixed-point scale, as a clip's (2⁻¹⁵).
constexpr float kRotationScale = 1.0F / 32768.0F;
// Bytes of a name field.
constexpr std::size_t kNameBytes = 16;

// Bounds-checked little-endian reads from one record; every read names the record in its error.
class RecordReader {
  public:
    RecordReader(std::span<const std::byte> data, std::string_view what) : m_data(data), m_what(what) {}

    // Fails unless `size` bytes at `offset` lie inside the record.
    [[nodiscard]] std::expected<void, Error> need(std::size_t offset, std::size_t size) const {
        if (offset > m_data.size() || size > m_data.size() - offset) {
            return fail(ErrorCode::Truncated, std::format("{}: {} bytes at {:#x} reach past its end ({:#x})", m_what,
                                                          size, offset, m_data.size()));
        }
        return {};
    }
    // The values at `offset`; the caller has checked the range with need().
    [[nodiscard]] std::uint8_t u8(std::size_t offset) const { return std::to_integer<std::uint8_t>(m_data[offset]); }
    [[nodiscard]] std::uint16_t u16(std::size_t offset) const {
        return static_cast<std::uint16_t>(u8(offset) | (u8(offset + 1) << 8U));
    }
    [[nodiscard]] std::uint32_t u32(std::size_t offset) const {
        return static_cast<std::uint32_t>(u16(offset)) | (static_cast<std::uint32_t>(u16(offset + 2)) << 16U);
    }
    [[nodiscard]] std::int16_t s16(std::size_t offset) const { return std::bit_cast<std::int16_t>(u16(offset)); }
    [[nodiscard]] float f32(std::size_t offset) const { return std::bit_cast<float>(u32(offset)); }
    // A NUL-terminated (or full-width) name of up to `size` bytes.
    [[nodiscard]] std::string text(std::size_t offset, std::size_t size) const {
        std::string out;
        for (std::size_t i = 0; i < size && m_data[offset + i] != std::byte{0}; ++i) {
            out.push_back(static_cast<char>(m_data[offset + i]));
        }
        return out;
    }
    [[nodiscard]] std::span<const std::byte> bytes(std::size_t offset, std::size_t size) const {
        return m_data.subspan(offset, size);
    }
    [[nodiscard]] std::string_view what() const { return m_what; }
    [[nodiscard]] std::size_t size() const { return m_data.size(); }

  private:
    std::span<const std::byte> m_data;
    std::string_view m_what;
};

// A quaternion from three stored s16 components, `w` rebuilt non-negative (0 when they square to more than 1).
anim::Quat rebuiltQuat(std::int16_t x, std::int16_t y, std::int16_t z) {
    const float fx = static_cast<float>(x) * kRotationScale;
    const float fy = static_cast<float>(y) * kRotationScale;
    const float fz = static_cast<float>(z) * kRotationScale;
    const float rest = 1.0F - (fx * fx) - (fy * fy) - (fz * fz);
    return anim::Quat{fx, fy, fz, rest > 0.0F ? std::sqrt(rest) : 0.0F};
}

// A pose: a position vec4 (w = 1, not read) then a quaternion.
ScenePose readPose(const RecordReader& r, std::size_t offset) {
    return ScenePose{.position = anim::Vec3{r.f32(offset), r.f32(offset + 4), r.f32(offset + 8)},
                     .rotation =
                         anim::Quat{r.f32(offset + 16), r.f32(offset + 20), r.f32(offset + 24), r.f32(offset + 28)}};
}

// A role-like definition: the name, the start pose at +0x10 and the end pose at +0x30. The caller checked its size.
SceneRole readRole(const RecordReader& r, std::size_t offset) {
    return SceneRole{
        .name = r.text(offset, kNameBytes), .start = readPose(r, offset + 0x10), .end = readPose(r, offset + 0x30)};
}

// `count` 24-byte events at `offset`.
std::expected<std::vector<SceneEvent>, Error> readEvents(const RecordReader& r, std::size_t offset, std::size_t count) {
    if (auto ok = r.need(offset, count * kEventBytes); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    std::vector<SceneEvent> events(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = offset + (i * kEventBytes);
        events[i].frame = r.u16(at);
        events[i].type = r.u16(at + 2);
        const std::span<const std::byte> args = r.bytes(at + 4, events[i].args.size());
        std::ranges::copy(args, events[i].args.begin());
    }
    return events;
}

// A role's clip at `offset`: the 80-byte descriptor, then (at the descriptor's +0x1c) sections A, B, C and the events.
std::expected<RoleClip, Error> readClip(const RecordReader& r, std::size_t offset) {
    if (auto ok = r.need(offset, anim::kClipDescriptorBytes); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    const std::size_t keyBytes =
        static_cast<std::size_t>(r.u16(offset + 0x10)) + r.u16(offset + 0x12) + r.u32(offset + 0x14);
    const std::size_t eventCount = r.u16(offset + 0x1a);
    const std::size_t keys = r.u32(offset + 0x1c);
    const std::size_t total = keyBytes + (eventCount * kEventBytes);
    if (auto ok = r.need(keys, total); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    auto clip = anim::parseAnimClip(r.bytes(offset, anim::kClipDescriptorBytes), r.bytes(keys, total));
    if (!clip) {
        return fail(clip.error().code,
                    std::format("{}: the clip at {:#x}: {}", r.what(), offset, clip.error().message));
    }
    auto events = readEvents(r, keys + keyBytes, eventCount);
    if (!events) {
        return std::unexpected(std::move(events.error()));
    }
    return RoleClip{.clip = std::move(*clip), .events = std::move(*events)};
}

// An object, camera or light track at `offset`: its header, then position keys, rotation keys and events.
std::expected<KeyTrack, Error> readTrack(const RecordReader& r, std::size_t offset) {
    if (auto ok = r.need(offset, kTrackHeaderBytes); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    KeyTrack track;
    track.duration = r.f32(offset + 4);
    const std::size_t total = r.u32(offset + 8);
    const std::size_t positionBytes = r.u32(offset + 0x0c);
    const std::size_t keys = r.u32(offset + 0x10);
    const std::size_t eventCount = r.u16(offset + 0x14);
    const std::size_t eventBytes = eventCount * kEventBytes;
    if (positionBytes > total || eventBytes > total - positionBytes || positionBytes % kPositionKeyBytes != 0 ||
        (total - positionBytes - eventBytes) % kRotationKeyBytes != 0 || !std::isfinite(track.duration) ||
        track.duration < 0.0F) {
        return fail(ErrorCode::Invalid, std::format("{}: the track at {:#x} has sizes {}, {} and {} events", r.what(),
                                                    offset, total, positionBytes, eventCount));
    }
    if (auto ok = r.need(keys, total); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    for (std::size_t at = keys; at < keys + positionBytes; at += kPositionKeyBytes) {
        track.positions.push_back(
            TrackPositionKey{.frame = r.u16(at), .value = anim::Vec3{r.f32(at + 4), r.f32(at + 8), r.f32(at + 12)}});
    }
    const std::size_t rotations = keys + positionBytes;
    for (std::size_t at = rotations; at < keys + total - eventBytes; at += kRotationKeyBytes) {
        track.rotations.push_back(
            TrackRotationKey{.frame = r.u16(at), .value = rebuiltQuat(r.s16(at + 2), r.s16(at + 4), r.s16(at + 6))});
    }
    auto events = readEvents(r, keys + total - eventBytes, eventCount);
    if (!events) {
        return std::unexpected(std::move(events.error()));
    }
    track.events = std::move(*events);
    return track;
}

// The `count` record offsets of the table at `table`.
std::expected<std::vector<std::size_t>, Error> readTable(const RecordReader& r, std::size_t table, std::size_t count) {
    if (auto ok = r.need(table, count * 4); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    std::vector<std::size_t> offsets(count);
    for (std::size_t i = 0; i < count; ++i) {
        offsets[i] = r.u32(table + (i * 4));
    }
    return offsets;
}

// The tracks the four tables at `tables` (record offsets of tables of track offsets) name, with `counts` of each.
std::expected<SceneTracks, Error> readTracks(const RecordReader& r, const std::array<std::size_t, 4>& tables,
                                             const std::array<std::size_t, 4>& counts) {
    SceneTracks tracks;
    auto clips = readTable(r, tables[0], counts[0]);
    if (!clips) {
        return std::unexpected(std::move(clips.error()));
    }
    for (const std::size_t at : *clips) {
        auto clip = readClip(r, at);
        if (!clip) {
            return std::unexpected(std::move(clip.error()));
        }
        tracks.clips.push_back(std::move(*clip));
    }
    // The keyed groups: objects, camera, lights.
    std::array<std::vector<KeyTrack>, 3> groups;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        auto offsets = readTable(r, tables[g + 1], counts[g + 1]);
        if (!offsets) {
            return std::unexpected(std::move(offsets.error()));
        }
        for (const std::size_t at : *offsets) {
            auto track = readTrack(r, at);
            if (!track) {
                return std::unexpected(std::move(track.error()));
            }
            groups.at(g).push_back(std::move(*track));
        }
    }
    tracks.objects = std::move(groups[0]);
    if (!groups[1].empty()) {
        tracks.camera = std::move(groups[1].front());
    }
    tracks.lights = std::move(groups[2]);
    return tracks;
}

// Fails unless the record's size word (+0) is its size.
std::expected<void, Error> checkSizeWord(const RecordReader& r) {
    if (r.u32(0) != r.size()) {
        return fail(ErrorCode::Invalid,
                    std::format("{}: its size word {} is not its size {}", r.what(), r.u32(0), r.size()));
    }
    return {};
}

// The index of the key in force at `frame` in keys sorted by frame: the last at or before it (0 before the first).
template <typename Key> std::size_t keyBefore(const std::vector<Key>& keys, float frame) {
    const auto after = std::ranges::upper_bound(keys, frame, std::less<>{},
                                                [](const Key& key) { return static_cast<float>(key.frame); });
    return after == keys.begin() ? 0 : static_cast<std::size_t>(after - keys.begin()) - 1;
}

// How far `frame` is from key `a` towards key `b`, 0-1 (0 when they share a frame).
float fraction(std::uint16_t a, std::uint16_t b, float frame) {
    if (b <= a) {
        return 0.0F;
    }
    return std::clamp((frame - static_cast<float>(a)) / static_cast<float>(b - a), 0.0F, 1.0F);
}

} // namespace

std::uint16_t SceneEvent::u16At(std::size_t offset) const {
    CONEY_ASSERT(offset >= 4 && offset + 2 <= kEventBytes);
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(args.at(offset - 4)) |
                                      (std::to_integer<unsigned>(args.at(offset - 3)) << 8U));
}

std::uint32_t SceneEvent::u32At(std::size_t offset) const {
    return static_cast<std::uint32_t>(u16At(offset)) | (static_cast<std::uint32_t>(u16At(offset + 2)) << 16U);
}

std::int16_t SceneEvent::s16At(std::size_t offset) const { return std::bit_cast<std::int16_t>(u16At(offset)); }

float SceneEvent::f32At(std::size_t offset) const { return std::bit_cast<float>(u32At(offset)); }

anim::Vec3 KeyTrack::positionAt(float frame) const {
    if (positions.empty()) {
        return {};
    }
    const std::size_t i = keyBefore(positions, frame);
    if (i + 1 >= positions.size() || frame <= static_cast<float>(positions[i].frame)) {
        return positions[i].value;
    }
    const TrackPositionKey& a = positions[i];
    const TrackPositionKey& b = positions[i + 1];
    return anim::lerp(a.value, b.value, fraction(a.frame, b.frame, frame));
}

anim::Quat KeyTrack::rotationAt(float frame) const {
    if (rotations.empty()) {
        return {};
    }
    const std::size_t i = keyBefore(rotations, frame);
    if (i + 1 >= rotations.size() || frame <= static_cast<float>(rotations[i].frame)) {
        return rotations[i].value;
    }
    // **Coney's choice**: slerp, the pose blender's interpolation; the page says "as quaternions" only.
    const TrackRotationKey& a = rotations[i];
    const TrackRotationKey& b = rotations[i + 1];
    return anim::slerp(a.value, b.value, fraction(a.frame, b.frame, frame));
}

float SceneTracks::duration() const {
    if (!clips.empty()) {
        return clips.front().clip.duration;
    }
    if (camera) {
        return camera->duration;
    }
    if (!objects.empty()) {
        return objects.front().duration;
    }
    return lights.empty() ? 0.0F : lights.front().duration;
}

bool isSceneHeader(std::span<const std::byte> record) {
    return record.size() >= 9 && record[4] == std::byte{0} && record[5] == std::byte{0} && record[6] == std::byte{0} &&
           record[7] == std::byte{0} && record[8] != std::byte{0};
}

std::expected<SceneHeader, Error> parseSceneHeader(std::span<const std::byte> record) {
    const RecordReader r(record, "scene header");
    if (auto ok = r.need(0, kHeaderBytes); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    if (auto ok = checkSizeWord(r); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    SceneHeader header;
    header.name = r.text(0x08, kNameBytes);
    header.firstSegment = r.text(0x18, 4);
    header.label = r.text(0x30, 0x54);
    header.frames = r.u32(0x84);
    const std::array<std::size_t, 4> counts{r.u8(0x20), r.u8(0x21), r.u8(0x22), r.u8(0x23)};
    if (counts[2] > 1) {
        return fail(ErrorCode::Invalid, std::format("scene {}: {} cameras", header.name, counts[2]));
    }
    // The definitions: roles and objects (0x60 each), the camera and the lights (0x70 each).
    const std::size_t roles = r.u32(0x90);
    const std::size_t objects = r.u32(0x94);
    const std::size_t camera = r.u32(0x98);
    const std::size_t lights = r.u32(0x9c);
    if (auto ok = r.need(roles, counts[0] * kRoleBytes); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    if (auto ok = r.need(objects, counts[1] * kRoleBytes); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    if (auto ok = r.need(camera, counts[2] * kCameraBytes); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    if (auto ok = r.need(lights, counts[3] * kLightBytes); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    for (std::size_t i = 0; i < counts[0]; ++i) {
        header.roles.push_back(readRole(r, roles + (i * kRoleBytes)));
    }
    for (std::size_t i = 0; i < counts[1]; ++i) {
        header.objects.push_back(readRole(r, objects + (i * kRoleBytes)));
    }
    if (counts[2] == 1) {
        header.camera = SceneCameraDef{.role = readRole(r, camera),
                                       .nearClip = r.f32(camera + 0x54),
                                       .farClip = r.f32(camera + 0x58),
                                       .preloadRadius = r.f32(camera + 0x60),
                                       .fieldOfView = r.f32(camera + 0x64)};
    }
    for (std::size_t i = 0; i < counts[3]; ++i) {
        const std::size_t at = lights + (i * kLightBytes);
        header.lights.push_back(SceneLightDef{.role = readRole(r, at),
                                              .kind = r.u32(at + 0x50),
                                              .colour = {r.f32(at + 0x54), r.f32(at + 0x58), r.f32(at + 0x5c)},
                                              .coneDegrees = r.f32(at + 0x64),
                                              .range = r.f32(at + 0x68)});
    }
    auto tracks = readTracks(r, {r.u32(0xa0), r.u32(0xa4), r.u32(0xa8), r.u32(0xac)}, counts);
    if (!tracks) {
        return fail(tracks.error().code, std::format("scene {}: {}", header.name, tracks.error().message));
    }
    header.tracks = std::move(*tracks);
    return header;
}

std::expected<SceneSegment, Error> parseSceneSegment(std::span<const std::byte> record) {
    const RecordReader r(record, "scene segment");
    if (auto ok = r.need(0, kSegmentHeaderBytes); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    if (auto ok = checkSizeWord(r); !ok) {
        return std::unexpected(std::move(ok.error()));
    }
    SceneSegment segment;
    segment.name = r.text(0x04, kNameBytes);
    segment.nextSegment = r.text(0x14, 4);
    const std::array<std::size_t, 4> counts{r.u8(0x18), r.u8(0x19), r.u8(0x1a), r.u8(0x1b)};
    if (counts[2] > 1) {
        return fail(ErrorCode::Invalid, std::format("segment {}: {} cameras", segment.name, counts[2]));
    }
    auto tracks = readTracks(r, {r.u32(0x1c), r.u32(0x20), r.u32(0x24), r.u32(0x28)}, counts);
    if (!tracks) {
        return fail(tracks.error().code, std::format("segment {}: {}", segment.name, tracks.error().message));
    }
    segment.tracks = std::move(*tracks);
    return segment;
}

std::string segmentName(std::string_view scene, std::string_view suffix) {
    return std::string(scene.substr(0, 15)).append(suffix.substr(0, 3));
}

std::optional<std::uint32_t> soundtrackOf(const SceneHeader& header) {
    constexpr std::uint16_t kSoundtrackEvent = 13;
    for (const RoleClip& clip : header.tracks.clips) {
        for (const SceneEvent& event : clip.events) {
            if (event.type == kSoundtrackEvent) {
                return event.u32At(8);
            }
        }
    }
    if (header.tracks.camera) {
        for (const SceneEvent& event : header.tracks.camera->events) {
            if (event.type == kSoundtrackEvent) {
                return event.u32At(8);
            }
        }
    }
    return std::nullopt;
}

float headingOf(anim::Quat rotation) { return 2.0F * std::atan2(rotation.z, rotation.w); }

} // namespace coney::scenes
