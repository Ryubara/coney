// SPDX-License-Identifier: GPL-3.0-or-later
// The replicated-field tables (docs/guides/multiplayer.md#field-tables): every kind's table is well formed and every
// field round-trips the ends and the middle of its range through the bit stream, within half a step; the reads refuse
// what the network could send wrong; and the tables' checks refuse a malformed table.
#include "net/replicated_fields.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "animation/anim_task.h"
#include "core/error.h"
#include "net/bit_stream.h"

using coney::ErrorCode;
using coney::net::BitReader;
using coney::net::BitWriter;
using coney::net::FieldEncoding;
using coney::net::FieldSpec;
using coney::net::FieldValue;
using coney::net::ReplicatedKind;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// The values a field must carry: its range's ends and its middle (both bool values, for a Bool; for an Angle, half a
// turn back, none and half a turn on, plus a value beyond a turn, which wraps).
std::vector<FieldValue> probes(const FieldSpec& field) {
    switch (field.encoding) {
    case FieldEncoding::Bool:
        return {false, true};
    case FieldEncoding::Integer: {
        const auto low = static_cast<std::int32_t>(field.min);
        const auto high = static_cast<std::int32_t>(field.max);
        return {low, low + ((high - low) / 2), high};
    }
    case FieldEncoding::Float:
        return {field.min, (field.min + field.max) / 2.0F, field.max};
    case FieldEncoding::Angle:
        return {-kPi, 0.0F, kPi, 2.5F * kPi};
    }
    return {};
}

// The distance between two angles around the circle, 0 to pi.
float angleDistance(float a, float b) { return std::abs(std::remainder(a - b, 2.0F * kPi)); }

// Whether `read` is `sent` as `field` carries it: exact for a Bool or an Integer, within half a step for a Float (its
// ends exact), within half a step around the circle for an Angle.
bool carried(const FieldSpec& field, const FieldValue& sent, const FieldValue& read) {
    if (sent.index() != read.index()) {
        return false;
    }
    const float halfStep = coney::net::fieldStep(field) / 2.0F;
    switch (field.encoding) {
    case FieldEncoding::Bool:
    case FieldEncoding::Integer:
        return sent == read;
    case FieldEncoding::Float: {
        const float value = std::get<float>(sent);
        if (value == field.min || value == field.max) {
            return std::get<float>(read) == value;
        }
        return std::abs(std::get<float>(read) - value) <= halfStep;
    }
    case FieldEncoding::Angle:
        return angleDistance(std::get<float>(read), std::get<float>(sent)) <= halfStep + 1e-6F;
    }
    return false;
}

} // namespace

// Expected: unverified, #553 (Coney's own field tables; the original has no network play)
TEST_CASE("field tables: every kind has a well-formed table the size of its field enum", "[net][fields]") {
    using coney::net::fieldTable;
    for (const ReplicatedKind kind : coney::net::kReplicatedKinds) {
        INFO(std::string(coney::net::replicatedKindName(kind)));
        CHECK_FALSE(fieldTable(kind).empty());
        CHECK(coney::net::validateFields(fieldTable(kind)).has_value());
    }
    CHECK(fieldTable(ReplicatedKind::Human).size() == static_cast<std::size_t>(coney::net::HumanField::Count));
    CHECK(fieldTable(ReplicatedKind::WorldObject).size() ==
          static_cast<std::size_t>(coney::net::WorldObjectField::Count));
    CHECK(fieldTable(ReplicatedKind::Car).size() == static_cast<std::size_t>(coney::net::CarField::Count));
}

// Expected: unverified, #553 (Coney's own field tables; the original has no network play)
TEST_CASE("field tables: every field round-trips its range's ends and middle", "[net][fields]") {
    for (const ReplicatedKind kind : coney::net::kReplicatedKinds) {
        const auto fields = coney::net::fieldTable(kind);
        REQUIRE_FALSE(fields.empty());
        // All of a kind's fields in one stream, each probe in turn, so a field that takes the wrong number of bits
        // shifts every field after it.
        BitWriter out;
        std::size_t probesWritten = 0;
        for (const FieldSpec& field : fields) {
            for (const FieldValue& value : probes(field)) {
                coney::net::writeField(out, field, value);
                ++probesWritten;
            }
        }
        BitReader in(out.bytes());
        std::size_t probesRead = 0;
        for (const FieldSpec& field : fields) {
            for (const FieldValue& value : probes(field)) {
                INFO(std::string(coney::net::replicatedKindName(kind)) + "." + std::string(field.name));
                const auto read = coney::net::readField(in, field);
                REQUIRE(read.has_value());
                CHECK(carried(field, value, *read));
                ++probesRead;
            }
        }
        CHECK(probesRead == probesWritten);
        CHECK(in.finish().has_value());
    }
}

// Expected: unverified, #553 (Coney's own field tables; the original has no network play)
TEST_CASE("field tables: a whole thing's bits are the sum of its fields' bits", "[net][fields]") {
    const auto fields = coney::net::fieldTable(ReplicatedKind::Human);
    REQUIRE_FALSE(fields.empty()); // an empty table would sum to 0 bits and prove nothing
    BitWriter out;
    std::size_t expected = 0;
    for (const FieldSpec& field : fields) {
        expected += static_cast<std::size_t>(field.bits);
        coney::net::writeField(out, field, probes(field).front());
    }
    CHECK(out.bitCount() == expected);
    CHECK(coney::net::fieldsBits(fields) == expected);
    // network-architecture.md#snapshots budgets a moving human's delta at 15 to 25 bytes; a full one, with its four
    // animation layers, is not far over.
    CHECK(coney::net::fieldsBits(fields) <= std::size_t{28} * 8);
}

// Expected: combat-reactions.md#damage, confirmed (runtime)
TEST_CASE("field tables: a human's health and maximum carry the original's s16, a lethal hit's -20 included",
          "[net][fields]") {
    // The original keeps both as s16 and a lethal hit took health from 3 to -20 (a dying human's health stays below
    // 0 until the reaction ends), so the fields must carry it.
    for (const char* name : {"health", "healthMax"}) {
        INFO(name);
        const FieldSpec* found = nullptr;
        for (const FieldSpec& field : coney::net::fieldTable(ReplicatedKind::Human)) {
            if (std::string_view(field.name) == name) {
                found = &field;
            }
        }
        REQUIRE(found != nullptr);
        // Checked before any write, so a range too narrow fails here rather than in writeField's assert.
        REQUIRE(found->encoding == FieldEncoding::Integer);
        REQUIRE(found->min <= -32768.0F);
        REQUIRE(found->max >= 32767.0F);
        BitWriter out;
        for (const std::int32_t health : {-20, -32768, 32767, 0}) {
            coney::net::writeField(out, *found, health);
        }
        BitReader in(out.bytes());
        for (const std::int32_t health : {-20, -32768, 32767, 0}) {
            CHECK(std::get<std::int32_t>(*coney::net::readField(in, *found)) == health);
        }
    }
}

// Expected: unverified, #553 (Coney's own field tables; the original has no network play)
TEST_CASE("field tables: a Float out of its range is clamped, and quantized to within half a step", "[net][fields]") {
    const FieldSpec field{.name = "f", .encoding = FieldEncoding::Float, .bits = 8, .min = -1.0F, .max = 1.0F};
    BitWriter out;
    coney::net::writeField(out, field, 5.0F);
    coney::net::writeField(out, field, 0.3F);
    BitReader in(out.bytes());
    CHECK(std::get<float>(*coney::net::readField(in, field)) == 1.0F);
    CHECK(std::abs(std::get<float>(*coney::net::readField(in, field)) - 0.3F) <= coney::net::fieldStep(field) / 2.0F);
    CHECK(coney::net::fieldStep(field) == 2.0F / 255.0F);
}

// Expected: unverified, #553 (Coney's own field tables; the original has no network play)
TEST_CASE("field tables: an Integer over its range, or a short read, is refused", "[net][fields]") {
    const FieldSpec field{.name = "n", .encoding = FieldEncoding::Integer, .bits = 4, .min = 10.0F, .max = 20.0F};
    BitWriter out;
    out.writeBits(11, 4); // 10 + 11 = 21, over the range; the 4 bits hold up to 15
    BitReader in(out.bytes());
    const auto over = coney::net::readField(in, field);
    REQUIRE_FALSE(over.has_value());
    CHECK(over.error().code == ErrorCode::Invalid);

    BitReader empty({});
    const auto shortRead = coney::net::readField(empty, field);
    REQUIRE_FALSE(shortRead.has_value());
    CHECK(shortRead.error().code == ErrorCode::Truncated);
}

// Expected: unverified, #553 (Coney's own field tables; the original has no network play)
TEST_CASE("field tables: the checks refuse a malformed table", "[net][fields]") {
    using coney::net::validateFields;
    const auto refused = [](std::span<const FieldSpec> fields) {
        const auto checked = validateFields(fields);
        return !checked.has_value() && checked.error().code == ErrorCode::Invalid;
    };
    const FieldSpec ok{.name = "a", .encoding = FieldEncoding::Bool, .bits = 1};
    CHECK(validateFields(std::array{ok}).has_value());
    CHECK(refused(std::array{ok, ok})); // the same name twice
    CHECK(refused(std::array{FieldSpec{.name = "", .encoding = FieldEncoding::Bool, .bits = 1}}));
    CHECK(refused(std::array{FieldSpec{.name = "b", .encoding = FieldEncoding::Bool, .bits = 2}}));
    CHECK(refused(std::array{FieldSpec{.name = "f", .encoding = FieldEncoding::Float, .bits = 0, .max = 1.0F}}));
    CHECK(refused(std::array{FieldSpec{.name = "f", .encoding = FieldEncoding::Float, .bits = 33, .max = 1.0F}}));
    CHECK(refused(std::array{FieldSpec{.name = "f", .encoding = FieldEncoding::Float, .bits = 8, .min = 1.0F}}));
    CHECK(refused(std::array{FieldSpec{.name = "n", .encoding = FieldEncoding::Integer, .bits = 3, .max = 8.0F}}));
    CHECK(refused(std::array{FieldSpec{.name = "n", .encoding = FieldEncoding::Integer, .bits = 3, .max = 6.5F}}));
    CHECK(validateFields(std::array{FieldSpec{
                             .name = "n", .encoding = FieldEncoding::Integer, .bits = 3, .min = -3.0F, .max = 4.0F}})
              .has_value());
}

// Expected: unverified, #554 (Coney's own field tables; an infinity has no place on the circle, as NaN has none)
TEST_CASE("field tables: an infinite angle goes to the first step, as NaN does", "[net][fields]") {
    const FieldSpec heading{.name = "heading", .encoding = FieldEncoding::Angle, .bits = 10};
    for (const float radians : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()}) {
        CHECK(coney::net::fieldWord(heading, radians) == coney::net::fieldWord(heading, -kPi));
        BitWriter out;
        coney::net::writeField(out, heading, radians);
        BitReader in(out.bytes());
        const auto read = coney::net::readField(in, heading);
        REQUIRE(read.has_value());
        CHECK(std::get<float>(*read) == -kPi);
    }
    // The first step is -pi's, word 0; no angle goes to it but -pi (or pi, the same angle) and these: none is half a
    // turn on, which is word 512 of 1,024.
    CHECK(coney::net::fieldWord(heading, -kPi) == 0);
    CHECK(coney::net::fieldWord(heading, 0.0F) == 512);
}

// Expected: unverified, #554 (Coney's own field table; AnimTaskStack's limits are its port of the original's task
// stack, docs/research/formats/animation.md#task-stack)
TEST_CASE("field tables: a human carries every animation layer the task stack shows at once", "[net][fields]") {
    // Changes that fade in over one another, none ending: the stack finishes the newest fade early once it holds more
    // than six tasks (four layers with their three fades), so four layers is the most it ever shows.
    coney::anim::AnimClip clip;
    clip.duration = 1.0F;
    coney::anim::AnimTaskStack stack;
    std::size_t most = 0;
    for (std::uint32_t id = 1; id <= coney::anim::AnimTaskStack::kMaxTasks; ++id) {
        stack.change(std::make_unique<coney::anim::LoopTask>(clip, id, 1.0F, 0U), 0.5F);
        most = std::max(most, stack.layerCount());
    }
    CHECK(most == 4);
    REQUIRE(coney::net::kHumanAnimLayers == most);
    // Over the newest layer's clip and playhead, each older layer's clip, its playhead and the fade over it.
    const auto fields = coney::net::fieldTable(ReplicatedKind::Human);
    const auto has = [&](const std::string& name) {
        return std::ranges::any_of(fields, [&](const FieldSpec& field) { return field.name == name; });
    };
    CHECK(has("clip"));
    CHECK(has("playhead"));
    for (std::size_t layer = 1; layer < most; ++layer) {
        CHECK(has("clip" + std::to_string(layer)));
        CHECK(has("playhead" + std::to_string(layer)));
        CHECK(has("fade" + std::to_string(layer)));
    }
}
