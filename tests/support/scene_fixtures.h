// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic scene records (`.scn`), built byte by byte in the tests. Nothing here comes from the game (LEGAL.md, "No
// game data"). The layout follows docs/research/scenes.md#data.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "support/character_fixtures.h"
#include "support/fixtures.h"

namespace coney::test {

/// A 24-byte scene event: frame and type, then the arguments (patch them with the setters).
class SceneEventBytes {
  public:
    SceneEventBytes(std::uint16_t frame, std::uint16_t type) { m_bytes.u16(frame).u16(type).padTo(24); }
    /// The f32, u32 or u16 at `offset` from the event's start.
    SceneEventBytes& f32At(std::size_t offset, float value) {
        return u32At(offset, std::bit_cast<std::uint32_t>(value));
    }
    SceneEventBytes& u32At(std::size_t offset, std::uint32_t value) {
        m_bytes.patchU32(offset, value);
        return *this;
    }
    SceneEventBytes& u16At(std::size_t offset, std::uint16_t value) {
        std::vector<std::byte> data = m_bytes.data();
        data.at(offset) = static_cast<std::byte>(value & 0xffU);
        data.at(offset + 1) = static_cast<std::byte>(value >> 8U);
        m_bytes = Bytes{};
        m_bytes.append(data);
        return *this;
    }
    [[nodiscard]] const Bytes& bytes() const { return m_bytes; }

  private:
    Bytes m_bytes;
};

/// An object, camera or light track: its duration, keys and events.
struct TrackSpec {
    float duration = 1.0F;
    std::vector<std::pair<std::uint16_t, anim::Vec3>> positions;
    std::vector<std::pair<std::uint16_t, std::array<std::int16_t, 3>>> rotations;
    std::vector<SceneEventBytes> events;
};

/// A role's clip: its duration, a constant forward root velocity (section A, m/s along +y), no rotation channels, and
/// its events.
struct ClipSpec {
    float duration = 1.0F;
    float forwardSpeed = 0.0F;
    std::vector<SceneEventBytes> events;
};

/// The tracks of one part.
struct PartSpec {
    std::vector<ClipSpec> clips;
    std::vector<TrackSpec> objects;
    std::optional<TrackSpec> camera;
    std::vector<TrackSpec> lights;
};

/// A role or object definition: name, start and end positions and headings (radians about z).
struct RoleSpec {
    std::string name;
    anim::Vec3 start;
    float startHeading = 0.0F;
    anim::Vec3 end;
    float endHeading = 0.0F;
};

namespace scene_detail {

// Appends a little-endian float.
inline Bytes& putF32(Bytes& b, float v) { return b.u32(std::bit_cast<std::uint32_t>(v)); }

// Appends a pose: a position vec4 and the quaternion of a heading about z.
inline void putPose(Bytes& b, anim::Vec3 p, float heading) {
    putF32(putF32(putF32(putF32(b, p.x), p.y), p.z), 1.0F);
    putF32(putF32(putF32(putF32(b, 0.0F), 0.0F), std::sin(heading / 2.0F)), std::cos(heading / 2.0F));
}

// Appends a role definition of `size` bytes (0x60 for a role or object, 0x70 for the camera or a light).
inline void putRole(Bytes& b, const RoleSpec& role, std::size_t size, std::span<const float> extra = {}) {
    const std::size_t at = b.size();
    b.text(role.name.substr(0, 15)).padTo(at + 16);
    putPose(b, role.start, role.startHeading);
    putPose(b, role.end, role.endHeading);
    for (const float value : extra) {
        putF32(b, value);
    }
    b.padTo(at + size);
}

// Appends a track and returns its offset.
inline std::uint32_t putTrack(Bytes& b, const TrackSpec& track) {
    b.padTo((b.size() + 3) / 4 * 4);
    const auto at = static_cast<std::uint32_t>(b.size());
    const std::uint32_t positionBytes = static_cast<std::uint32_t>(track.positions.size() * 16);
    const std::uint32_t total = positionBytes + static_cast<std::uint32_t>(track.rotations.size() * 8) +
                                static_cast<std::uint32_t>(track.events.size() * 24);
    b.u32(0);
    putF32(b, track.duration);
    b.u32(total).u32(positionBytes).u32(at + 0x18).u16(static_cast<std::uint16_t>(track.events.size()));
    b.padTo(at + 0x18);
    for (const auto& [frame, value] : track.positions) {
        b.u16(frame).u16(0);
        putF32(putF32(putF32(b, value.x), value.y), value.z);
    }
    for (const auto& [frame, value] : track.rotations) {
        b.u16(frame).u16(static_cast<std::uint16_t>(value[0])).u16(static_cast<std::uint16_t>(value[1]));
        b.u16(static_cast<std::uint16_t>(value[2]));
    }
    for (const SceneEventBytes& event : track.events) {
        b.append(event.bytes().span());
    }
    return at;
}

// Appends a clip (descriptor, then section A of two keys, section B of one key, then events) and returns its offset.
inline std::uint32_t putClip(Bytes& b, const ClipSpec& clip) {
    b.padTo((b.size() + 3) / 4 * 4);
    const auto at = static_cast<std::uint32_t>(b.size());
    const auto frames = static_cast<std::uint16_t>(std::lround(clip.duration * 30.0F));
    Bytes descriptor = clipDescriptor(ClipFields{.name = "scene_clip",
                                                 .duration = clip.duration,
                                                 .events = static_cast<std::uint16_t>(clip.events.size())},
                                      16, 8, 0);
    descriptor.patchU32(0x1c, at + 80);
    b.append(descriptor.span());
    // Section A: a channel of two keys of the forward speed; section B: the pelvis at the origin.
    const auto speed = static_cast<std::uint16_t>(static_cast<std::int16_t>(clip.forwardSpeed * 1023.0F));
    b.u8(0).u8(0).u16(0).u16(speed).u16(0);
    b.u8(static_cast<std::uint8_t>(std::min<std::uint16_t>(frames, 255))).u8(0).u16(0).u16(speed).u16(0);
    b.u8(0).u8(0).u16(0).u16(0).u16(0);
    for (const SceneEventBytes& event : clip.events) {
        b.append(event.bytes().span());
    }
    return at;
}

// Appends a part's tracks and their four tables; returns the tables' offsets.
inline std::array<std::uint32_t, 4> putPart(Bytes& b, const PartSpec& part) {
    std::array<std::vector<std::uint32_t>, 4> offsets;
    for (const ClipSpec& clip : part.clips) {
        offsets[0].push_back(putClip(b, clip));
    }
    for (const TrackSpec& track : part.objects) {
        offsets[1].push_back(putTrack(b, track));
    }
    if (part.camera) {
        offsets[2].push_back(putTrack(b, *part.camera));
    }
    for (const TrackSpec& track : part.lights) {
        offsets[3].push_back(putTrack(b, track));
    }
    b.padTo((b.size() + 3) / 4 * 4);
    std::array<std::uint32_t, 4> tables{};
    for (std::size_t g = 0; g < 4; ++g) {
        tables.at(g) = static_cast<std::uint32_t>(b.size());
        for (const std::uint32_t offset : offsets.at(g)) {
            b.u32(offset);
        }
        b.u32(0); // a table is never empty in the file, which keeps an empty one inside the record
    }
    return tables;
}

} // namespace scene_detail

/// What a header record holds besides its first part.
struct SceneSpec {
    std::string name = "tst_c1";
    std::string firstSegment;
    std::uint32_t frames = 30;
    std::vector<RoleSpec> roles;
    std::vector<RoleSpec> objects;
    std::optional<RoleSpec> camera; ///< With near 0.5, far 75, radius 500 and a field of view of 60.
    std::vector<RoleSpec> lights;   ///< Kind 1, white, a 40° cone, range 0.
    PartSpec part;
};

/// A header record.
inline Bytes sceneHeaderRecord(const SceneSpec& spec) {
    using namespace scene_detail;
    Bytes b;
    b.u32(0).u32(0).text(spec.name.substr(0, 15)).padTo(0x18).text(spec.firstSegment.substr(0, 3)).padTo(0x20);
    b.u8(static_cast<std::uint8_t>(spec.roles.size())).u8(static_cast<std::uint8_t>(spec.objects.size()));
    b.u8(spec.camera ? 1 : 0).u8(static_cast<std::uint8_t>(spec.lights.size()));
    b.padTo(0x30).text(spec.name.substr(0, 15)).padTo(0x84).u32(spec.frames).padTo(0xc0);
    std::array<std::uint32_t, 4> defs{};
    defs[0] = static_cast<std::uint32_t>(b.size());
    for (const RoleSpec& role : spec.roles) {
        putRole(b, role, 0x60);
    }
    defs[1] = static_cast<std::uint32_t>(b.size());
    for (const RoleSpec& object : spec.objects) {
        putRole(b, object, 0x60);
    }
    defs[2] = static_cast<std::uint32_t>(b.size());
    if (spec.camera) {
        const std::array<float, 6> lens{0.0F, 0.5F, 75.0F, 0.0F, 500.0F, 60.0F};
        putRole(b, *spec.camera, 0x70, lens);
    }
    defs[3] = static_cast<std::uint32_t>(b.size());
    for (const RoleSpec& light : spec.lights) {
        const std::array<float, 7> values{std::bit_cast<float>(1U), 1.0F, 1.0F, 1.0F, 0.0F, 40.0F, 0.0F};
        putRole(b, light, 0x70, values);
    }
    const std::array<std::uint32_t, 4> tables = putPart(b, spec.part);
    for (std::size_t i = 0; i < 4; ++i) {
        b.patchU32(0x90 + (4 * i), defs.at(i));
        b.patchU32(0xa0 + (4 * i), tables.at(i));
    }
    b.patchU32(0xb0, defs[0]);
    b.patchU32(0, static_cast<std::uint32_t>(b.size()));
    return b;
}

/// A segment record named `name` (the scene's name and the suffix), continuing with `next` (empty for the last).
inline Bytes sceneSegmentRecord(std::string_view name, std::string_view next, const PartSpec& part) {
    using namespace scene_detail;
    Bytes b;
    b.u32(0).text(name.substr(0, 15)).padTo(0x14).text(next.substr(0, 3)).padTo(0x18);
    b.u8(static_cast<std::uint8_t>(part.clips.size())).u8(static_cast<std::uint8_t>(part.objects.size()));
    b.u8(part.camera ? 1 : 0).u8(static_cast<std::uint8_t>(part.lights.size())).padTo(0x2c);
    const std::array<std::uint32_t, 4> tables = putPart(b, part);
    for (std::size_t i = 0; i < 4; ++i) {
        b.patchU32(0x1c + (4 * i), tables.at(i));
    }
    b.patchU32(0, static_cast<std::uint32_t>(b.size()));
    return b;
}

/// A scene list chunk's data: a count, then `{id, size, name[16]}` per name, ids from 0.
inline Bytes sceneListChunk(std::span<const std::string> names) {
    Bytes b;
    b.u32(static_cast<std::uint32_t>(names.size()));
    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::size_t at = b.size();
        b.u32(static_cast<std::uint32_t>(i)).u32(0).text(std::string_view(names[i]).substr(0, 16)).padTo(at + 24);
    }
    return b;
}

/// A header with two roles, an object, a camera and a light, continued in a segment `aa` (1 s, then 0.5 s): role 0
/// walks forward at 1 m/s from its marks (events 21 and 22 at frame 0) and is put at (100, 100) at scene frame 40;
/// the camera fades out at scene frame 40 (an event of the header's track past its part).
inline SceneSpec twoPartSpec() {
    SceneSpec spec;
    spec.firstSegment = "aa";
    spec.frames = 45;
    spec.roles = {RoleSpec{"warrtest", {10.0F, 20.0F, 0.0F}, 0.5F, {12.0F, 21.0F, 0.0F}, 1.0F},
                  RoleSpec{"warrother", {0.0F, 0.0F, 0.0F}, 0.0F, {1.0F, 1.0F, 0.0F}, 0.0F}};
    spec.objects = {RoleSpec{"box", {5.0F, 5.0F, 0.0F}, 0.0F, {6.0F, 6.0F, 0.0F}, 0.0F}};
    spec.camera = RoleSpec{"camera", {0.0F, -5.0F, 2.0F}, 0.0F, {0.0F, -5.0F, 2.0F}, 0.0F};
    spec.lights = {RoleSpec{"fspot01", {1.0F, 2.0F, 3.0F}, 0.0F, {1.0F, 2.0F, 3.0F}, 0.0F}};
    spec.part.clips = {ClipSpec{.duration = 1.0F,
                                .forwardSpeed = 1.0F,
                                .events = {SceneEventBytes(0, 21).f32At(8, 10.0F).f32At(12, 20.0F).f32At(16, 0.0F),
                                           SceneEventBytes(0, 22).f32At(8, 0.5F),
                                           SceneEventBytes(40, 21).f32At(8, 100.0F).f32At(12, 100.0F)}},
                       ClipSpec{.duration = 1.0F, .forwardSpeed = 0.0F, .events = {}}};
    spec.part.objects = {TrackSpec{.duration = 1.0F,
                                   .positions = {{0, {0.0F, 0.0F, 0.0F}}, {30, {3.0F, 0.0F, 0.0F}}},
                                   .rotations = {},
                                   .events = {SceneEventBytes(5, 24)}}};
    spec.part.camera =
        TrackSpec{.duration = 1.0F,
                  .positions = {{0, {0.0F, -5.0F, 2.0F}}},
                  .rotations = {{0, {0, 0, 0}}},
                  .events = {SceneEventBytes(0, 28).f32At(8, 0.5F),
                             SceneEventBytes(15, 26).f32At(8, 40.0F).f32At(12, 0.2F).f32At(16, 50.0F),
                             SceneEventBytes(10, 41).u16At(4, 0), SceneEventBytes(20, 13).u32At(8, 0x1234),
                             SceneEventBytes(40, 27).f32At(8, 0.25F)}};
    spec.part.lights = {
        TrackSpec{.duration = 1.0F, .positions = {{0, {1.0F, 2.0F, 3.0F}}}, .rotations = {}, .events = {}}};
    return spec;
}

/// The segment `tst_c1aa` that continues twoPartSpec(): half a second of every track.
inline PartSpec segmentPart() {
    PartSpec part;
    part.clips = {ClipSpec{.duration = 0.5F, .forwardSpeed = 1.0F, .events = {}},
                  ClipSpec{.duration = 0.5F, .forwardSpeed = 0.0F, .events = {}}};
    part.objects = {TrackSpec{.duration = 0.5F,
                              .positions = {{0, {3.0F, 0.0F, 0.0F}}, {15, {4.0F, 0.0F, 0.0F}}},
                              .rotations = {},
                              .events = {}}};
    part.camera = TrackSpec{.duration = 0.5F, .positions = {{0, {0.0F, -5.0F, 2.0F}}}, .rotations = {}, .events = {}};
    part.lights = {TrackSpec{.duration = 0.5F, .positions = {}, .rotations = {}, .events = {}}};
    return part;
}

} // namespace coney::test
