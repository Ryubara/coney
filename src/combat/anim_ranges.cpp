// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/anim_ranges.h"

#include <string>

#include "fileio/reader.h"

namespace coney::combat {

namespace {

// The s16 fields that hold thousandths.
constexpr float kMilli = 0.001F;
// When the far range is 0 the reach stands for it, scaled by this (docs/research/formats/animation.md).
constexpr float kFarFromReach = 1.25F;

// Reads one 16-byte record; the caller has checked the bytes are there.
AnimRange readRecord(io::Reader& reader) {
    AnimRange range;
    range.directionX = static_cast<float>(static_cast<std::int16_t>(reader.readU16Le().value_or(0))) * kMilli;
    range.directionY = static_cast<float>(static_cast<std::int16_t>(reader.readU16Le().value_or(0))) * kMilli;
    range.reach = reader.readF32Le().value_or(0.0F);
    range.far = static_cast<float>(static_cast<std::int16_t>(reader.readU16Le().value_or(0))) * kMilli;
    range.damage = static_cast<std::int16_t>(reader.readU16Le().value_or(0));
    range.kind = static_cast<std::int16_t>(reader.readU16Le().value_or(0));
    range.flags = reader.readU16Le().value_or(0);
    return range;
}

} // namespace

std::expected<AnimRangeList, Error> AnimRangeList::parse(std::span<const std::byte> chunk) {
    io::Reader reader(chunk);
    auto count = reader.readU32Le();
    if (!count) {
        return std::unexpected(count.error());
    }
    // Check the size before reserving, so a corrupt count cannot ask for gigabytes.
    if (reader.remaining() / kRecordBytes < *count) {
        return std::unexpected(Error{ErrorCode::Truncated, "Anim Range List is shorter than its count of " +
                                                               std::to_string(*count) + " records"});
    }
    AnimRangeList list;
    list.m_records.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        list.m_records.push_back(readRecord(reader));
    }
    return list;
}

const AnimRange* AnimRangeList::find(std::size_t id) const { return id < m_records.size() ? &m_records[id] : nullptr; }

int AnimRangeList::damage(std::size_t id) const {
    const AnimRange* range = find(id);
    return range != nullptr ? range->damage : 0;
}

bool AnimRangeList::setDamage(std::size_t id, std::int16_t damage) {
    if (id >= m_records.size()) {
        return false;
    }
    m_records[id].damage = damage;
    return true;
}

float AnimRangeList::farRange(std::size_t id) const {
    const AnimRange* range = find(id);
    if (range == nullptr) {
        return 0.0F;
    }
    return range->far != 0.0F ? range->far : range->reach * kFarFromReach;
}

} // namespace coney::combat
