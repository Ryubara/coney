// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenes/scene_list.h"

#include <algorithm>
#include <format>
#include <utility>

namespace coney::scenes {

namespace {

// Bytes of one record and of its name field.
constexpr std::size_t kRecordBytes = 24;
constexpr std::size_t kNameBytes = 16;

// The little-endian u32 at `offset`; the caller has checked the range.
std::uint32_t u32At(std::span<const std::byte> data, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(data[offset + i]) << (8U * i);
    }
    return value;
}

} // namespace

std::expected<SceneList, Error> SceneList::parse(std::span<const std::byte> chunk) {
    if (chunk.size() < 4) {
        return fail(ErrorCode::Truncated, "scene list: no count");
    }
    const std::size_t count = u32At(chunk, 0);
    if (count > (chunk.size() - 4) / kRecordBytes) {
        return fail(ErrorCode::Truncated,
                    std::format("scene list: {} records do not fit in {} bytes", count, chunk.size()));
    }
    std::vector<SceneListEntry> entries;
    entries.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = 4 + (i * kRecordBytes);
        SceneListEntry entry{.id = u32At(chunk, at), .size = u32At(chunk, at + 4), .name = {}};
        for (std::size_t c = 0; c < kNameBytes && chunk[at + 8 + c] != std::byte{0}; ++c) {
            entry.name.push_back(static_cast<char>(chunk[at + 8 + c]));
        }
        entries.push_back(std::move(entry));
    }
    return SceneList(std::move(entries));
}

std::optional<std::uint32_t> SceneList::findContaining(std::string_view name) const {
    const auto found = std::ranges::find_if(
        m_entries, [name](const SceneListEntry& entry) { return entry.name.find(name) != std::string::npos; });
    return found == m_entries.end() ? std::nullopt : std::optional(found->id);
}

std::optional<std::uint32_t> SceneList::findExact(std::string_view name) const {
    const auto found = std::ranges::find(m_entries, name, &SceneListEntry::name);
    return found == m_entries.end() ? std::nullopt : std::optional(found->id);
}

const SceneListEntry* SceneList::entry(std::uint32_t id) const {
    const auto found = std::ranges::find(m_entries, id, &SceneListEntry::id);
    return found == m_entries.end() ? nullptr : &*found;
}

} // namespace coney::scenes
