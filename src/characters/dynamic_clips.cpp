// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/dynamic_clips.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

#include "gamemodes/load_entry_mode.h"

namespace coney::characters {

namespace {

// The file extension of an animation resource.
constexpr std::string_view kAnimExtension = ".anm";

} // namespace

std::expected<anim::AnimClip, Error> loadAnimResource(const io::Wad& wad, std::string_view name) {
    std::string file(name);
    if (!file.ends_with(kAnimExtension)) {
        file += kAnimExtension;
    }
    auto entry = wad.lookup(file);
    if (!entry) {
        return std::unexpected(std::move(entry.error()));
    }
    // The resource through the chunk system: its Anim Data handler leaves the clip on the object stack.
    chunk::ChunkHandlerTable handlers = chunk::ChunkHandlerTable::withDefaults();
    handlers.setHandlers(anim::kAnimDataChunk, chunk::ChunkHandlers{anim::onAnimDataLoaded, {}});
    auto load = loadWadEntry(wad, **entry, handlers);
    if (!load) {
        return std::unexpected(std::move(load.error()));
    }
    auto object = load->stacks.popObject<anim::AnimClipObject>();
    if (!object) {
        return fail(ErrorCode::NotFound, std::format("{} builds no clip: {}", file, object.error().message));
    }
    return std::move((*object)->clip());
}

const anim::AnimClip* DynamicClips::find(std::string_view name) {
    if (name.empty()) {
        return nullptr;
    }
    if (const auto found = m_clips.find(name); found != m_clips.end()) {
        return found->second.get();
    }
    std::unique_ptr<anim::AnimClip> clip;
    if (m_load) {
        if (auto loaded = m_load(name)) {
            clip = std::make_unique<anim::AnimClip>(std::move(*loaded));
        }
    }
    return m_clips.emplace(std::string(name), std::move(clip)).first->second.get();
}

std::size_t DynamicClips::failures() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_clips, [](const auto& entry) { return !entry.second; }));
}

} // namespace coney::characters
