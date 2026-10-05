// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/anim_set.h"

#include <cmath>
#include <span>

#include "fileio/reader.h"

namespace coney::characters {

namespace {

// The Anim Range List: a count word, then one 16-byte record per anim id; the flags are at +0x0e of a record.
constexpr std::size_t kRangeListHeader = 4;
constexpr std::size_t kRangeRecordBytes = 16;
constexpr std::size_t kRangeFlagsOffset = 0x0e;

} // namespace

const anim::AnimClip* AnimSet::clip(std::size_t id) const {
    if (const anim::AnimClip* own = m_own->animation(id); own != nullptr) {
        return own;
    }
    return m_defaults != nullptr ? m_defaults->animation(id) : nullptr;
}

std::uint16_t AnimSet::rangeFlags(std::size_t id) const {
    const std::span<const std::byte> list = m_own->rangeList();
    const std::size_t at = kRangeListHeader + id * kRangeRecordBytes + kRangeFlagsOffset;
    if (at + 2 > list.size()) {
        return 0;
    }
    io::Reader reader(list.subspan(at, 2));
    return reader.readU16Le().value_or(0);
}

float AnimSet::rate(std::size_t id) const {
    const std::uint16_t flags = rangeFlags(id);
    if ((flags & kRangeRate800) != 0) {
        return kAnimSpeeds[1];
    }
    if ((flags & kRangeRate1000) != 0) {
        return kAnimSpeeds[2];
    }
    if ((flags & kRangeRate2000) != 0) {
        return kAnimSpeeds[3];
    }
    return kAnimSpeeds[0];
}

float AnimSet::speed(std::size_t id) const {
    const anim::AnimClip* found = clip(id);
    if (found == nullptr || found->duration <= 0.0F) {
        return 0.0F;
    }
    return std::hypot(found->displacement.x, found->displacement.y) * rate(id) / found->duration;
}

} // namespace coney::characters
