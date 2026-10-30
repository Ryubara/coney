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

/// A human's fields, indexes into fieldTable(ReplicatedKind::Human).
/// @coney-only network play; the original has none
enum class HumanField : std::uint8_t {
    PositionX,  ///< The feet, metres.
    PositionY,  ///< The feet, metres.
    PositionZ,  ///< The feet, metres (up).
    Heading,    ///< Radians, 0 facing +y.
    Health,     ///< Health points (record `+0x144`, a 16-bit number; docs/research/characters.md).
    HealthMax,  ///< Its maximum (record `+0x146`).
    Clip,       ///< The anim id playing.
    Playhead,   ///< How far through the clip, 0 to 1.
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
