// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_data.h"

#include <algorithm>
#include <format>
#include <map>
#include <utility>

#include "fileio/reader.h"
#include "gamemodes/load_entry_mode.h"

namespace coney::characters {

const anim::AnimClip* CharacterData::animation(std::size_t id) const {
    if (id >= kAnimIds || m_slots[id] == kDefaultAnimSlot) {
        return nullptr;
    }
    return m_clips[m_slots[id]].get();
}

const anim::AnimClip* CharacterData::findClip(std::string_view name) const {
    const auto found = std::ranges::find_if(m_clips, [name](const auto& clip) { return clip->name == name; });
    return found == m_clips.end() ? nullptr : found->get();
}

std::expected<CharacterData, Error> CharacterData::resolve(std::span<const std::byte> dataChunk,
                                                           std::vector<std::byte> rangeList,
                                                           chunk::ChunkStacks& stacks) {
    if (dataChunk.size() != kCharacterDataBytes) {
        return fail(ErrorCode::Invalid, std::format("a Character Data chunk holds {} bytes, not {}", dataChunk.size(),
                                                    kCharacterDataBytes));
    }
    CharacterData data;
    data.m_rangeList = std::move(rangeList);
    // Read the slots, and collect the distinct values; the size check above covers every read.
    std::map<std::uint32_t, std::uint32_t> clipOfSlotValue;
    for (std::size_t id = 0; id < kAnimIds; ++id) {
        const std::uint32_t slot = io::loadU32Le(dataChunk.subspan(kAnimSlotsOffset + id * 4, 4));
        if (slot != kDefaultAnimSlot) {
            if (slot >= kAnimIds) {
                return fail(ErrorCode::Invalid,
                            std::format("anim id {} names slot {}, beyond the {} ids", id, slot, kAnimIds));
            }
            clipOfSlotValue.emplace(slot, 0);
        }
        data.m_slots[id] = slot;
    }
    // Each distinct value, smallest first, takes the next clip off the object stack.
    for (auto& [value, clip] : clipOfSlotValue) {
        auto object = stacks.popObject<anim::AnimClipObject>();
        if (!object) {
            return fail(ErrorCode::Invalid,
                        std::format("slot value {} has no animation left to take: {}", value, object.error().message));
        }
        clip = static_cast<std::uint32_t>(data.m_clips.size());
        data.m_clips.push_back(std::make_unique<anim::AnimClip>(std::move((*object)->clip())));
    }
    for (std::uint32_t& slot : data.m_slots) {
        if (slot != kDefaultAnimSlot) {
            slot = clipOfSlotValue.at(slot);
        }
    }
    return data;
}

std::expected<void, Error> onCharacterDataLoaded(chunk::ChunkStacks& stacks, std::uint32_t type) {
    auto dataChunk = stacks.popChunk(type);
    if (!dataChunk) {
        return std::unexpected(std::move(dataChunk.error()));
    }
    auto rangeList = stacks.popChunk(kAnimRangeListChunk);
    if (!rangeList) {
        return std::unexpected(std::move(rangeList.error()));
    }
    auto data = CharacterData::resolve(dataChunk->bytes, std::move(rangeList->bytes), stacks);
    if (!data) {
        return std::unexpected(std::move(data.error()));
    }
    stacks.pushObject(std::make_unique<CharacterDataObject>(std::move(*data)));
    return {};
}

void addCharacterDataHandlers(chunk::ChunkHandlerTable& table) {
    table.setHandlers(anim::kAnimDataChunk, chunk::ChunkHandlers{anim::onAnimDataLoaded, {}});
    table.setHandlers(kCharacterDataChunk, chunk::ChunkHandlers{onCharacterDataLoaded, {}});
}

std::expected<CharacterData, Error> loadCharacterData(const io::Wad& wad, const io::WadEntry& entry,
                                                      const chunk::ChunkHandlerTable& table) {
    auto load = loadWadEntry(wad, entry, table);
    if (!load) {
        return std::unexpected(std::move(load.error()));
    }
    auto object = load->stacks.popObject<CharacterDataObject>();
    if (!object) {
        return fail(ErrorCode::NotFound, std::format("the entry builds no character data: {}", object.error().message));
    }
    return std::move((*object)->data());
}

} // namespace coney::characters
