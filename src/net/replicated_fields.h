// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <variant>

#include "core/error.h"
#include "net/bit_stream.h"

// The replicated-field tables: for each kind of replicated thing, its fields' names, encodings, bits and ranges,
// declared once so the snapshot encoder, its decoder and the tests read one definition, as Quake 3's netField_t
// tables do. Coney's own; the original has no network play (docs/guides/multiplayer.md#field-tables).

namespace coney::net {

/// How a field is encoded.
/// @coney-only network play; the original has none
enum class FieldEncoding : std::uint8_t {
    Bool,    ///< One bit.
    Integer, ///< A whole number in [min, max], sent as its distance from min in `bits` bits; exact.
    Float,   ///< A number clamped to [min, max] and quantized to `bits` bits (quantize()); ends exact, within half a
             ///< step.
    Angle,   ///< Radians, any value, wrapped onto one turn and quantized to `bits` bits; -pi and pi are the same angle.
};

/// One field of a replicated kind. `min` and `max` are the range of an Integer or Float field (whole numbers for an
/// Integer); a Bool and an Angle field leave them 0.
/// @coney-only network play; the original has none
struct FieldSpec {
    std::string_view name;
    FieldEncoding encoding = FieldEncoding::Bool;
    int bits = 1;
    float min = 0.0F;
    float max = 0.0F;
};

/// A field's value: a bool for a Bool field, a whole number for an Integer one, radians or the number for an Angle or
/// Float one.
/// @coney-only network play; the original has none
using FieldValue = std::variant<bool, std::int32_t, float>;

/// Writes `value` as `field` says. A value of the wrong alternative, or an Integer out of its range, is a programmer
/// error (CONEY_ASSERT), since the writer encodes our own state; a Float out of range is clamped, as quantize() does.
/// @coney-only network play; the original has none
void writeField(BitWriter& writer, const FieldSpec& field, const FieldValue& value);

/// The bits writeField() writes for `value`, as a number below 2^bits: what a snapshot compares to see whether a field
/// changed (a change too small to reach the next step costs nothing) and what it sends. The same programmer errors
/// as writeField().
/// @coney-only network play; the original has none
[[nodiscard]] std::uint32_t fieldWord(const FieldSpec& field, const FieldValue& value);

/// The value a fieldWord() `word` stands for. Fails with Invalid when the word is over the field's bits, or an
/// Integer's over its range, since the word comes from the network.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<FieldValue, Error> fieldValue(const FieldSpec& field, std::uint32_t word);

/// Reads a writeField() value of `field`. Fails with Truncated past the end, and with Invalid when an Integer is over
/// its range (the bits hold more than the range has), since the bytes come from the network.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<FieldValue, Error> readField(BitReader& reader, const FieldSpec& field);

/// The step between two neighbouring values `field` can carry: what a round trip may be off by is half of it (0 for a
/// Bool or an Integer field, which are exact).
/// @coney-only network play; the original has none
[[nodiscard]] float fieldStep(const FieldSpec& field);

/// Checks a table: every field named, the names different, its bits 1 to 32 (1 for a Bool); a Float's range not
/// empty; an Integer's range whole numbers that fit its bits. Invalid with the first field's name otherwise.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<void, Error> validateFields(std::span<const FieldSpec> fields);

/// Bits one thing takes with every field written, without the changed bits a snapshot adds.
/// @coney-only network play; the original has none
[[nodiscard]] std::size_t fieldsBits(std::span<const FieldSpec> fields);

/// The kinds of replicated thing that carry fields (docs/guides/network-architecture.md#net-ids): a camera is named by
/// its player's slot and a sound, a particle or a message is an event, so none of those has a table.
/// @coney-only network play; the original has none
enum class ReplicatedKind : std::uint8_t {
    Human,       ///< A player or an AI human.
    WorldObject, ///< A door, a prop, glass, a pick-up or a loose object, once changed from the level's data.
    Car,         ///< A car.
};

/// Every replicated kind, in order.
inline constexpr std::array<ReplicatedKind, 3> kReplicatedKinds{ReplicatedKind::Human, ReplicatedKind::WorldObject,
                                                                ReplicatedKind::Car};

/// A kind's name, for logs and test names.
/// @coney-only network play; the original has none
[[nodiscard]] std::string_view replicatedKindName(ReplicatedKind kind);

/// A kind's field table, in field order (the order of its field enum below).
/// @coney-only network play; the original has none
[[nodiscard]] std::span<const FieldSpec> fieldTable(ReplicatedKind kind);

/// The animation layers a human carries: the newest, and the older ones it and the layers between fade in over. As
/// many as `anim::AnimTaskStack` shows at once (through overlapping fades), so a guest draws every blend the host does.
/// @coney-only network play; the original has none
inline constexpr std::size_t kHumanAnimLayers = 4;

/// A human's fields, indexes into fieldTable(ReplicatedKind::Human).
/// @coney-only network play; the original has none
enum class HumanField : std::uint8_t {
    PositionX,  ///< The feet, metres.
    PositionY,  ///< The feet, metres.
    PositionZ,  ///< The feet, metres (up).
    Heading,    ///< Radians, 0 facing +y.
    Health,     ///< Health points (record `+0x144`, a 16-bit number; docs/research/characters.md).
    HealthMax,  ///< Its maximum (record `+0x146`).
    Clip,       ///< The anim id playing: the newest animation layer.
    Playhead,   ///< How far through the clip, 0 to 1.
    Clip1,      ///< The anim id of the layer under the newest, which the newest fades in over; 0 with none.
    Playhead1,  ///< How far through layer 1's clip, 0 to 1; 0 with no layer 1.
    Fade1,      ///< How far the newest layer has faded in over layer 1 and the older ones, 0 to 1; 1 with no layer 1.
    Clip2,      ///< The anim id of layer 2, under layer 1, which layer 1 fades in over; 0 with none.
    Playhead2,  ///< How far through layer 2's clip, 0 to 1; 0 with no layer 2.
    Fade2,      ///< How far layer 1 has faded in over layer 2 and the older ones, 0 to 1; 1 with no layer 2.
    Clip3,      ///< The anim id of layer 3, the oldest, under layer 2; 0 with none.
    Playhead3,  ///< How far through layer 3's clip, 0 to 1; 0 with no layer 3.
    Fade3,      ///< How far layer 2 has faded in over layer 3, 0 to 1; 1 with no layer 3.
    Grounded,   ///< On the ground (not jumping or falling).
    KnockedOut, ///< Knocked out or wounded.
    Count,
};

/// A world object's fields, indexes into fieldTable(ReplicatedKind::WorldObject).
/// @coney-only network play; the original has none
enum class WorldObjectField : std::uint8_t {
    PositionX, ///< Metres.
    PositionY, ///< Metres.
    PositionZ, ///< Metres (up).
    Heading,   ///< Radians.
    Opening,   ///< A door's swing, radians.
    State,     ///< The object's own state number (a pick-up taken, glass cracked, ...).
    Broken,    ///< Broken.
    Hidden,    ///< Hidden (taken away, not drawn).
    Count,
};

/// A car's fields, indexes into fieldTable(ReplicatedKind::Car).
/// @coney-only network play; the original has none
enum class CarField : std::uint8_t {
    PositionX, ///< Metres.
    PositionY, ///< Metres.
    PositionZ, ///< Metres (up).
    Heading,   ///< Radians.
    Speed,     ///< Metres a second along its heading; negative backing up.
    Count,
};

} // namespace coney::net
