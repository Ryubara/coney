// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenes/scene_cache.h"

#include <algorithm>
#include <format>

namespace coney::scenes {

namespace {

// Whether a slot's scene is idle: loaded and not playing, or over.
bool idle(const SceneSlot& slot) { return slot.state == SceneState::Loaded || slot.state == SceneState::Ended; }

} // namespace

void SceneCache::clear(SceneSlot& slot) { slot = SceneSlot{}; }

SceneSlot* SceneCache::find(std::uint32_t id) {
    const auto found = std::ranges::find_if(
        m_slots, [id](const SceneSlot& slot) { return slot.state != SceneState::Empty && slot.id == id; });
    return found == m_slots.end() ? nullptr : &*found;
}

const SceneSlot* SceneCache::find(std::uint32_t id) const {
    const auto found = std::ranges::find_if(
        m_slots, [id](const SceneSlot& slot) { return slot.state != SceneState::Empty && slot.id == id; });
    return found == m_slots.end() ? nullptr : &*found;
}

SceneSlot* SceneCache::request(std::uint32_t id, std::string_view callback, std::uint64_t nowMs) {
    if (m_list->entry(id) == nullptr) {
        return nullptr;
    }
    // A scene loaded or loading gets one more user and nothing else.
    if (SceneSlot* held = find(id); held != nullptr) {
        ++held->users;
        held->lastRequestMs = nowMs;
        return held;
    }
    // An empty slot, else one whose scene has ended, else the least recently requested idle one.
    SceneSlot* slot = nullptr;
    if (auto empty = std::ranges::find(m_slots, SceneState::Empty, &SceneSlot::state); empty != m_slots.end()) {
        slot = &*empty;
    } else if (auto ended = std::ranges::find(m_slots, SceneState::Ended, &SceneSlot::state); ended != m_slots.end()) {
        slot = &*ended;
    } else {
        for (SceneSlot& candidate : m_slots) {
            if (idle(candidate) && (slot == nullptr || candidate.lastRequestMs < slot->lastRequestMs)) {
                slot = &candidate;
            }
        }
    }
    if (slot == nullptr) {
        return nullptr;
    }
    clear(*slot);
    slot->id = id;
    slot->state = SceneState::Loading;
    slot->callback = std::string(callback);
    slot->lastRequestMs = nowMs;
    return slot;
}

bool SceneCache::readHeader(SceneSlot& slot) {
    const SceneListEntry* entry = m_list->entry(slot.id);
    auto bytes = entry != nullptr ? m_source(entry->name)
                                  : std::expected<std::vector<std::byte>, Error>(fail(ErrorCode::NotFound, "no id"));
    auto header = bytes ? parseSceneHeader(*bytes) : std::expected<SceneHeader, Error>(std::unexpected(bytes.error()));
    if (!header) {
        m_lastError = std::format("scene {} ({}): {}", slot.id, entry != nullptr ? entry->name : std::string("?"),
                                  header.error().message);
        clear(slot);
        return false;
    }
    ++m_headersRead;
    slot.header = std::make_unique<SceneHeader>(std::move(*header));
    slot.roleHandles.assign(slot.header->roles.size(), 0.0);
    slot.objectHandles.assign(slot.header->objects.size(), 0.0);
    slot.suffixes.assign(1, slot.header->firstSegment);
    slot.state = SceneState::Loaded;
    // The arrival adds the first user (0x00353158); a request made while the file loaded has added one already.
    ++slot.users;
    slot.callbackDue = !slot.callback.empty();
    if (m_onLoaded) {
        m_onLoaded(*slot.header);
    }
    return true;
}

std::vector<std::pair<std::string, std::uint32_t>> SceneCache::service() {
    std::vector<std::pair<std::string, std::uint32_t>> calls;
    for (SceneSlot& slot : m_slots) {
        if (slot.state == SceneState::Loading) {
            readHeader(slot);
        }
        if (slot.state != SceneState::Empty && slot.callbackDue) {
            slot.callbackDue = false;
            calls.emplace_back(slot.callback, slot.id);
        }
    }
    return calls;
}

std::expected<SceneSlot*, Error> SceneCache::loadNow(std::uint32_t id, std::uint64_t nowMs) {
    SceneSlot* slot = find(id);
    if (slot == nullptr) {
        slot = request(id, {}, nowMs);
    }
    if (slot == nullptr) {
        return fail(ErrorCode::NotFound, std::format("scene {}: no slot is free, or no such scene", id));
    }
    if (slot->state == SceneState::Loading && !readHeader(*slot)) {
        return fail(ErrorCode::Invalid, m_lastError);
    }
    return slot;
}

void SceneCache::unload(std::uint32_t id) {
    SceneSlot* slot = find(id);
    if (slot == nullptr) {
        return;
    }
    if (slot->users > 0) {
        --slot->users;
    }
    // The last user gone: the slot is emptied. Otherwise another request still holds the record, idle again.
    if (slot->users == 0) {
        clear(*slot);
    } else {
        slot->state = SceneState::Loaded;
    }
}

bool SceneCache::hasPart(const SceneSlot& slot, std::size_t part) {
    if (slot.header == nullptr) {
        return false;
    }
    return part == 0 || (part <= slot.suffixes.size() && !slot.suffixes[part - 1].empty());
}

bool SceneCache::requestNext(SceneSlot& slot) {
    const std::size_t next = slot.newestPart() + 1;
    if (!hasPart(slot, next)) {
        return false;
    }
    // Toggle the buffer (the first segment goes into buffer 0) and free what it held.
    const std::size_t buffer = slot.current ? 1 - *slot.current : 0;
    slot.buffers.at(buffer).reset();
    slot.bufferPart.at(buffer) = 0;
    const std::string name = segmentName(slot.header->name, slot.suffixes[next - 1]);
    auto bytes = m_source(name);
    auto segment =
        bytes ? parseSceneSegment(*bytes) : std::expected<SceneSegment, Error>(std::unexpected(bytes.error()));
    if (!segment) {
        m_lastError = std::format("segment {}: {}", name, segment.error().message);
        // The chain ends here: no part after the newest.
        slot.suffixes.resize(next - 1);
        slot.suffixes.emplace_back();
        return false;
    }
    ++m_segmentsRead;
    slot.suffixes.resize(next);
    slot.suffixes.push_back(segment->nextSegment);
    slot.buffers.at(buffer) = std::make_unique<SceneSegment>(std::move(*segment));
    slot.bufferPart.at(buffer) = next;
    slot.current = buffer;
    return true;
}

void SceneCache::restartChain(SceneSlot& slot) {
    for (std::size_t b = 0; b < slot.buffers.size(); ++b) {
        slot.buffers.at(b).reset();
        slot.bufferPart.at(b) = 0;
    }
    slot.current.reset();
    if (slot.header != nullptr) {
        slot.suffixes.assign(1, slot.header->firstSegment);
    }
}

const SceneTracks* SceneCache::part(SceneSlot& slot, std::size_t part) {
    if (slot.header == nullptr) {
        return nullptr;
    }
    if (part == 0) {
        return &slot.header->tracks;
    }
    for (std::size_t b = 0; b < slot.buffers.size(); ++b) {
        if (slot.bufferPart.at(b) == part && slot.buffers.at(b) != nullptr) {
            return &slot.buffers.at(b)->tracks;
        }
    }
    // The next part, still on its way: wait for it.
    if (part == slot.newestPart() + 1 && requestNext(slot) && slot.current) {
        return &slot.buffers.at(*slot.current)->tracks;
    }
    return nullptr;
}

} // namespace coney::scenes
