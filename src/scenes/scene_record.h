// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "core/error.h"

// The in-engine scenes' records (`<name>.scn`): a header record that names a scene's roles, objects, camera and
// lights with their start and end poses and holds the first part of its tracks, and, for a long scene, a chain of
// segment records that hold the later parts. Decoded once at load into plain values; every offset is checked against
// the record, so a damaged record fails to load instead of being read past its end.
// Format: docs/research/scenes.md#data

namespace coney::scenes {

/// Frames a second the scene tracks are keyed in and the scene task runs at.
inline constexpr float kSceneFrameRate = 30.0F;

/// Bytes of the fixed parts: a header record's (up to its definitions), a role or object definition, a camera or light
/// definition, a segment record's header, an object / camera / light track's header, a position key, a rotation key
/// and an event.
inline constexpr std::size_t kHeaderBytes = 0xb4;
inline constexpr std::size_t kRoleBytes = 0x60;
inline constexpr std::size_t kCameraBytes = 0x70;
inline constexpr std::size_t kLightBytes = 0x70;
inline constexpr std::size_t kSegmentHeaderBytes = 0x2c;
inline constexpr std::size_t kTrackHeaderBytes = 0x16;
inline constexpr std::size_t kPositionKeyBytes = 16;
inline constexpr std::size_t kRotationKeyBytes = 8;
inline constexpr std::size_t kEventBytes = 24;

/// A place in the scene's space: a position (metres, game axes, z up) and an orientation.
struct ScenePose {
    anim::Vec3 position;
    anim::Quat rotation;
};

/// A human role or an object of a scene: its name (a role's is a model name, `warrcl`) and its poses at the scene's
/// start and end. Binding is by index, not by name.
struct SceneRole {
    std::string name;
    ScenePose start;
    ScenePose end;
};

/// The scene's camera: a role with a lens. `+0x50`, `+0x5c` and `+0x68` are not read by the code traced.
struct SceneCameraDef {
    SceneRole role;
    float nearClip = 0.0F;      ///< `+0x54`.
    float farClip = 0.0F;       ///< `+0x58`.
    float preloadRadius = 0.0F; ///< `+0x60`: the world preload's radius around the camera's start.
    float fieldOfView = 0.0F;   ///< `+0x64`, degrees.
};

/// A light of the scene, made when it starts.
struct SceneLightDef {
    SceneRole role;
    std::uint32_t kind = 0;                            ///< `+0x50`: 1, 2 or another value (the light's flags).
    std::array<float, 3> colour{};                     ///< `+0x54`: red, green, blue, 0-1.
    float coneDegrees = 0.0F;                          ///< `+0x64`: the full cone angle.
    float range = 0.0F;                                ///< `+0x68`: 0 means kDefaultLightRange.
    static constexpr float kDefaultLightRange = 15.0F; ///< The range a light of range 0 gets.
};

/// One event of a track or of a role's clip, as stored: its frame, its type and the 20 bytes of its arguments, read by
/// the accessors at their offsets from the event's start (the frame is `+0`, the type `+2`).
struct SceneEvent {
    std::uint16_t frame = 0;
    std::uint16_t type = 0;
    std::array<std::byte, kEventBytes - 4> args{}; ///< Bytes `+4` to `+0x17`.

    /// The u16, u32, s16 or f32 at `offset` (4 to 0x16 or 0x14) from the event's start.
    [[nodiscard]] std::uint16_t u16At(std::size_t offset) const;
    [[nodiscard]] std::uint32_t u32At(std::size_t offset) const;
    [[nodiscard]] std::int16_t s16At(std::size_t offset) const;
    [[nodiscard]] float f32At(std::size_t offset) const;
};

/// A position key of an object, camera or light track: `u16 frame`, two unused bytes, `f32 x, y, z`.
struct TrackPositionKey {
    std::uint16_t frame = 0;
    anim::Vec3 value;
};

/// A rotation key: `u16 frame`, `s16 x, y, z` × 2⁻¹⁵, `w` rebuilt non-negative as a clip's.
struct TrackRotationKey {
    std::uint16_t frame = 0;
    anim::Quat value;
};

/// An object's, the camera's or a light's track for one part of a scene.
struct KeyTrack {
    float duration = 0.0F; ///< Seconds.
    std::vector<TrackPositionKey> positions;
    std::vector<TrackRotationKey> rotations;
    std::vector<SceneEvent> events;

    /// The position at `frame` (fractional, from the part's start): lerped between the keys either side, the first
    /// key's before it and the last's after it; the origin with no keys.
    /// @orig 0x00355ab8 SceneTrack_StepKeys (SceneCache.cpp)
    [[nodiscard]] anim::Vec3 positionAt(float frame) const;
    /// The rotation at `frame`, interpolated as quaternions between the keys either side; no rotation with no keys.
    [[nodiscard]] anim::Quat rotationAt(float frame) const;
};

/// A human role's track for one part: an ordinary animation clip, and its events as stored (the clip's own decoding
/// scales the event arguments as keys, which the scene events 21 and 22 do not use: they hold floats).
struct RoleClip {
    anim::AnimClip clip;
    std::vector<SceneEvent> events;
};

/// The tracks of one part (a header or a segment): a clip per role, then a track per object, the camera's and a track
/// per light, in the header's definition order.
struct SceneTracks {
    std::vector<RoleClip> clips;
    std::vector<KeyTrack> objects;
    std::optional<KeyTrack> camera;
    std::vector<KeyTrack> lights;

    /// The part's length in seconds: its first track's duration (clips first, then the camera, the objects and the
    /// lights), 0 with none. `SceneLength` reports this for the header's part.
    /// @orig 0x003540b8 Scene_Length (SceneCache.cpp)
    [[nodiscard]] float duration() const;
};

/// A decoded header record.
struct SceneHeader {
    std::string name;         ///< `+0x08`.
    std::string firstSegment; ///< `+0x18`: the first segment's suffix (`aa`); empty for a one-record scene.
    std::string label;        ///< `+0x30`: a name that no code read (the scene's own in about half the records).
    std::uint32_t frames = 0; ///< `+0x84`: the whole scene's length in 1/30 s frames, segments included.
    std::vector<SceneRole> roles;
    std::vector<SceneRole> objects;
    std::optional<SceneCameraDef> camera;
    std::vector<SceneLightDef> lights;
    SceneTracks tracks;

    /// Whether the scene continues in segment records.
    [[nodiscard]] bool hasSegments() const { return !firstSegment.empty(); }
};

/// A decoded segment record: the next part of the header's tracks, in the same order.
struct SceneSegment {
    std::string name;        ///< `+0x04`: the scene's name cut to 15 characters, then the suffix.
    std::string nextSegment; ///< `+0x14`: the next segment's suffix; empty for the last.
    SceneTracks tracks;
};

/// Whether `record` is a header (`+0x04` is 0 and a name follows at `+0x08`) rather than a segment.
[[nodiscard]] bool isSceneHeader(std::span<const std::byte> record);

/// Decodes a header record. Fails with ErrorCode::Truncated when the record is shorter than its fixed part or an
/// offset, table, key or event reaches past its end, and with ErrorCode::Invalid when the size word is not the
/// record's size, the camera count is above 1, a track's sizes are not whole keys, or a role's clip does not decode
/// (anim::parseAnimClip()).
/// @orig 0x00352098 SceneRecord_Fixup (SceneCache.cpp)
[[nodiscard]] std::expected<SceneHeader, Error> parseSceneHeader(std::span<const std::byte> record);

/// Decodes a segment record. Fails as parseSceneHeader() does.
/// @orig 0x00352788 SceneSegment_Fixup (SceneCache.cpp)
[[nodiscard]] std::expected<SceneSegment, Error> parseSceneSegment(std::span<const std::byte> record);

/// The name the game looks a segment up by: the scene's name cut to 15 characters, then up to 3 characters of the
/// suffix (`strncpy(name, 15) + strncat(suffix, 3)`).
/// @orig 0x00352c08 Scene_RequestSegment (SceneCache.cpp)
[[nodiscard]] std::string segmentName(std::string_view scene, std::string_view suffix);

/// The scene soundtrack's name hash (`+8` of an event 13): the first role clip's event 13, in role order, else the
/// camera track's; nothing when neither has one (docs/research/sound.md#scene-sound).
/// @orig 0x00101c58 Scene_FindClipSoundtrack (unknown)
/// @orig 0x00354d28 Scene_FindTrackSoundtrack (SceneCache.cpp)
[[nodiscard]] std::optional<std::uint32_t> soundtrackOf(const SceneHeader& header);

/// The heading, radians about z (0 facing +y), of a scene orientation: `2 · atan2(z, w)`, which is how the role clips'
/// first event 22 holds a role's start heading.
[[nodiscard]] float headingOf(anim::Quat rotation);

} // namespace coney::scenes
