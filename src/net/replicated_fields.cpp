// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/replicated_fields.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <numbers>
#include <span>
#include <string_view>
#include <variant>

#include "animation/anim_task.h"
#include "core/assert.h"
#include "core/error.h"
#include "net/bit_stream.h"

namespace coney::net {

namespace {

constexpr double kTurn = 2.0 * std::numbers::pi;

// Steps of an Angle field of `bits` bits in one turn: 2^bits, as a double so 32 bits do not overflow.
double angleSteps(int bits) { return std::ldexp(1.0, bits); }

// The number of values an Integer field's range holds, less one: its largest encoded number.
std::uint64_t integerSpan(const FieldSpec& field) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(field.max) - static_cast<std::int64_t>(field.min));
}

// The largest number `bits` bits hold, 1 to 32 bits.
std::uint64_t maxForBits(int bits) { return (std::uint64_t{1} << static_cast<unsigned>(bits)) - 1U; }

// --- The tables. Every range here is Coney's choice for its own network play; the original sends nothing.

// Positions, metres: a level fits in 2 km across, and 18 bits give a step under 8 mm (docs/guides/
// network-architecture.md#snapshots). **Coney stand-in**: fixed bounds around the origin, until the client's world
// (#555), which loads the level on the guest, takes each level's own bounds.
constexpr float kPositionMin = -1024.0F;
constexpr float kPositionMax = 1024.0F;
constexpr int kPositionBits = 18;
// A heading to 10 bits, about a third of a degree.
constexpr int kHeadingBits = 10;

// A human carries every layer its task stack can show at once: change() finishes the newest fade early when more than
// kEarlyFinishTasks tasks (a fade counting as one) are held, so at most three layers stand under the new one
// (docs/guides/snapshots.md#human-state). The newest layer is a clip, a playhead and a gait value; each older one the
// same and a fade.
static_assert(kHumanAnimLayers == (anim::AnimTaskStack::kEarlyFinishTasks / 2) + 1);
static_assert(static_cast<std::size_t>(HumanField::Fade3) + 1 ==
              static_cast<std::size_t>(HumanField::Gait) + 1 + ((kHumanAnimLayers - 1) * 4));
// A gait blend's value runs from 0 to 4 (walk, jog, run, sprint, sprint). To 10 bits a step is 0.004, so a value
// arrives within 0.002: the sampler's thresholds on its fraction (0.01, 0.995) fall on the same side for the guest.
constexpr float kGaitMax = anim::GaitBlendTask::kMaxValue;
constexpr int kGaitBits = 10;

constexpr std::array<FieldSpec, static_cast<std::size_t>(HumanField::Count)> kHumanFields{{
    {.name = "positionX",
     .encoding = FieldEncoding::Float,
     .bits = kPositionBits,
     .min = kPositionMin,
     .max = kPositionMax},
    {.name = "positionY",
     .encoding = FieldEncoding::Float,
     .bits = kPositionBits,
     .min = kPositionMin,
     .max = kPositionMax},
    {.name = "positionZ",
     .encoding = FieldEncoding::Float,
     .bits = kPositionBits,
     .min = kPositionMin,
     .max = kPositionMax},
    {.name = "heading", .encoding = FieldEncoding::Angle, .bits = kHeadingBits},
    // Health and its maximum are s16 in the original's per-player record (docs/research/characters.md) and a lethal hit
    // takes health below 0 (combat-reactions.md#damage: 3 to -20), so the whole s16 range, exact.
    {.name = "health", .encoding = FieldEncoding::Integer, .bits = 16, .min = -32768.0F, .max = 32767.0F},
    {.name = "healthMax", .encoding = FieldEncoding::Integer, .bits = 16, .min = -32768.0F, .max = 32767.0F},
    // **Coney stand-in**: 12 bits hold every anim id Coney plays today (under 1000); the client's world (#555), which
    // plays the clips on the guest, may name a clip by the game's own hash instead.
    {.name = "clip", .encoding = FieldEncoding::Integer, .bits = 12, .min = 0.0F, .max = 4095.0F},
    // A share of the clip rather than a frame, since the table cannot know a clip's frame count: 10 bits are a frame
    // in a clip of a thousand.
    {.name = "playhead", .encoding = FieldEncoding::Float, .bits = 10, .min = 0.0F, .max = 1.0F},
    // The clip id names a gait blend's leading clip only: the value says which two of its clips play and how they
    // blend, so a guest rebuilds a walk or a run from the human's gait clips and it.
    {.name = "gait", .encoding = FieldEncoding::Float, .bits = kGaitBits, .min = 0.0F, .max = kGaitMax},
    // The older layers, as many as AnimTaskStack shows at once through overlapping fades: each one's clip, playhead
    // and gait value, and how far the layer above it has faded in over it, to 10 bits as the playheads.
    {.name = "clip1", .encoding = FieldEncoding::Integer, .bits = 12, .min = 0.0F, .max = 4095.0F},
    {.name = "playhead1", .encoding = FieldEncoding::Float, .bits = 10, .min = 0.0F, .max = 1.0F},
    {.name = "gait1", .encoding = FieldEncoding::Float, .bits = kGaitBits, .min = 0.0F, .max = kGaitMax},
    {.name = "fade1", .encoding = FieldEncoding::Float, .bits = 10, .min = 0.0F, .max = 1.0F},
    {.name = "clip2", .encoding = FieldEncoding::Integer, .bits = 12, .min = 0.0F, .max = 4095.0F},
    {.name = "playhead2", .encoding = FieldEncoding::Float, .bits = 10, .min = 0.0F, .max = 1.0F},
    {.name = "gait2", .encoding = FieldEncoding::Float, .bits = kGaitBits, .min = 0.0F, .max = kGaitMax},
    {.name = "fade2", .encoding = FieldEncoding::Float, .bits = 10, .min = 0.0F, .max = 1.0F},
    {.name = "clip3", .encoding = FieldEncoding::Integer, .bits = 12, .min = 0.0F, .max = 4095.0F},
    {.name = "playhead3", .encoding = FieldEncoding::Float, .bits = 10, .min = 0.0F, .max = 1.0F},
    {.name = "gait3", .encoding = FieldEncoding::Float, .bits = kGaitBits, .min = 0.0F, .max = kGaitMax},
    {.name = "fade3", .encoding = FieldEncoding::Float, .bits = 10, .min = 0.0F, .max = 1.0F},
    {.name = "grounded", .encoding = FieldEncoding::Bool, .bits = 1},
    {.name = "knockedOut", .encoding = FieldEncoding::Bool, .bits = 1},
}};

constexpr std::array<FieldSpec, static_cast<std::size_t>(WorldObjectField::Count)> kWorldObjectFields{{
    {.name = "positionX",
     .encoding = FieldEncoding::Float,
     .bits = kPositionBits,
     .min = kPositionMin,
     .max = kPositionMax},
    {.name = "positionY",
     .encoding = FieldEncoding::Float,
     .bits = kPositionBits,
     .min = kPositionMin,
     .max = kPositionMax},
    {.name = "positionZ",
     .encoding = FieldEncoding::Float,
     .bits = kPositionBits,
     .min = kPositionMin,
     .max = kPositionMax},
    {.name = "heading", .encoding = FieldEncoding::Angle, .bits = kHeadingBits},
    {.name = "opening", .encoding = FieldEncoding::Angle, .bits = kHeadingBits},
    // **Coney stand-in**: a byte for the object's own state until each object kind's states are replicated.
    {.name = "state", .encoding = FieldEncoding::Integer, .bits = 8, .min = 0.0F, .max = 255.0F},
    {.name = "broken", .encoding = FieldEncoding::Bool, .bits = 1},
    {.name = "hidden", .encoding = FieldEncoding::Bool, .bits = 1},
}};

constexpr std::array<FieldSpec, static_cast<std::size_t>(CarField::Count)> kCarFields{{
    {.name = "positionX",
     .encoding = FieldEncoding::Float,
     .bits = kPositionBits,
     .min = kPositionMin,
     .max = kPositionMax},
    {.name = "positionY",
     .encoding = FieldEncoding::Float,
     .bits = kPositionBits,
     .min = kPositionMin,
     .max = kPositionMax},
    {.name = "positionZ",
     .encoding = FieldEncoding::Float,
     .bits = kPositionBits,
     .min = kPositionMin,
     .max = kPositionMax},
    {.name = "heading", .encoding = FieldEncoding::Angle, .bits = kHeadingBits},
    // **Coney stand-in**: up to 64 m/s either way, about 3 cm/s a step, until the cars' own top speeds are replicated.
    {.name = "speed", .encoding = FieldEncoding::Float, .bits = 12, .min = -64.0F, .max = 64.0F},
}};

} // namespace

std::uint32_t fieldWord(const FieldSpec& field, const FieldValue& value) {
    switch (field.encoding) {
    case FieldEncoding::Bool:
        CONEY_ASSERT(std::holds_alternative<bool>(value));
        return std::get<bool>(value) ? 1U : 0U;
    case FieldEncoding::Integer: {
        CONEY_ASSERT(std::holds_alternative<std::int32_t>(value));
        const std::int64_t number = std::get<std::int32_t>(value);
        const auto low = static_cast<std::int64_t>(field.min);
        CONEY_ASSERT(number >= low && number <= static_cast<std::int64_t>(field.max));
        return static_cast<std::uint32_t>(number - low);
    }
    case FieldEncoding::Float:
        CONEY_ASSERT(std::holds_alternative<float>(value));
        return quantize(std::get<float>(value), field.min, field.max, field.bits);
    case FieldEncoding::Angle: {
        CONEY_ASSERT(std::holds_alternative<float>(value));
        const float radians = std::get<float>(value);
        // The angle as a share of a turn from -pi, wrapped into [0, 1), rounded to a step; the last step rounds up to a
        // whole turn, which is the first. NaN and an infinity, which have no place on the circle, go to the first
        // step, as quantize() sends NaN to the bottom (an infinity would wrap to NaN, and NaN has no whole number).
        const double share = std::isfinite(radians) ? (static_cast<double>(radians) + std::numbers::pi) / kTurn : 0.0;
        const double steps = angleSteps(field.bits);
        const double rounded = std::round((share - std::floor(share)) * steps);
        return static_cast<std::uint32_t>(rounded >= steps ? 0.0 : rounded);
    }
    }
    CONEY_ASSERT(false);
    return 0;
}

std::expected<FieldValue, Error> fieldValue(const FieldSpec& field, std::uint32_t word) {
    if (static_cast<std::uint64_t>(word) > maxForBits(field.bits)) {
        return fail(ErrorCode::Invalid, std::format("field {}: {} is over its {} bits", field.name, word, field.bits));
    }
    switch (field.encoding) {
    case FieldEncoding::Bool:
        return FieldValue{word != 0};
    case FieldEncoding::Integer:
        // The bits may hold more than the range does: more is a corrupt or forged packet.
        if (word > integerSpan(field)) {
            return fail(ErrorCode::Invalid, std::format("field {}: {} is over its range", field.name, word));
        }
        return FieldValue{static_cast<std::int32_t>(static_cast<std::int64_t>(field.min) + word)};
    case FieldEncoding::Float:
        return FieldValue{dequantize(word, field.min, field.max, field.bits)};
    case FieldEncoding::Angle:
        return FieldValue{
            static_cast<float>((static_cast<double>(word) / angleSteps(field.bits) * kTurn) - std::numbers::pi)};
    }
    return fail(ErrorCode::Invalid, std::format("field {}: unknown encoding", field.name));
}

void writeField(BitWriter& writer, const FieldSpec& field, const FieldValue& value) {
    writer.writeBits(fieldWord(field, value), field.bits);
}

std::expected<FieldValue, Error> readField(BitReader& reader, const FieldSpec& field) {
    const auto bits = reader.readBits(field.bits);
    if (!bits) {
        return std::unexpected(bits.error());
    }
    return fieldValue(field, *bits);
}

float fieldStep(const FieldSpec& field) {
    switch (field.encoding) {
    case FieldEncoding::Bool:
    case FieldEncoding::Integer:
        return 0.0F;
    case FieldEncoding::Float:
        return static_cast<float>((static_cast<double>(field.max) - field.min) /
                                  static_cast<double>(maxForBits(field.bits)));
    case FieldEncoding::Angle:
        return static_cast<float>(kTurn / angleSteps(field.bits));
    }
    return 0.0F;
}

std::expected<void, Error> validateFields(std::span<const FieldSpec> fields) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const FieldSpec& field = fields[i];
        const auto refuse = [&](std::string_view why) {
            return fail(ErrorCode::Invalid, std::format("field {} ({}): {}", i, field.name, why));
        };
        if (field.name.empty()) {
            return refuse("no name");
        }
        for (std::size_t before = 0; before < i; ++before) {
            if (fields[before].name == field.name) {
                return refuse("named twice");
            }
        }
        if (field.bits < 1 || field.bits > 32 || (field.encoding == FieldEncoding::Bool && field.bits != 1)) {
            return refuse("bits out of range");
        }
        if ((field.encoding == FieldEncoding::Float || field.encoding == FieldEncoding::Integer) &&
            !(field.max > field.min)) {
            return refuse("empty range");
        }
        if (field.encoding == FieldEncoding::Integer) {
            if (std::trunc(field.min) != field.min || std::trunc(field.max) != field.max) {
                return refuse("range not whole numbers");
            }
            // Its values are 32-bit numbers (FieldValue).
            if (field.min < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
                field.max >= 2147483648.0F) { // 2^31, one past the largest
                return refuse("range beyond 32-bit numbers");
            }
            if (integerSpan(field) > maxForBits(field.bits)) {
                return refuse("range wider than its bits");
            }
        }
    }
    return {};
}

std::size_t fieldsBits(std::span<const FieldSpec> fields) {
    std::size_t bits = 0;
    for (const FieldSpec& field : fields) {
        bits += static_cast<std::size_t>(field.bits);
    }
    return bits;
}

std::string_view replicatedKindName(ReplicatedKind kind) {
    switch (kind) {
    case ReplicatedKind::Human:
        return "human";
    case ReplicatedKind::WorldObject:
        return "worldObject";
    case ReplicatedKind::Car:
        return "car";
    }
    return "unknown";
}

std::span<const FieldSpec> fieldTable(ReplicatedKind kind) {
    switch (kind) {
    case ReplicatedKind::Human:
        return kHumanFields;
    case ReplicatedKind::WorldObject:
        return kWorldObjectFields;
    case ReplicatedKind::Car:
        return kCarFields;
    }
    return {};
}

} // namespace coney::net
