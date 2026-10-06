// SPDX-License-Identifier: GPL-3.0-or-later
#include "movies/disc_captions.h"

#include <cctype>
#include <format>
#include <utility>
#include <vector>

#include "core/chunk_stacks.h"
#include "core/chunk_system.h"
#include "core/chunk_types.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_record.h"
#include "world/level_object.h"

namespace coney::movies {

namespace {

// The Subtitles chunk of the level file `file`, or nothing when it cannot be read or has none. Every other chunk is
// skipped unread.
std::optional<std::vector<std::byte>> readSubtitlesChunk(const io::Wad& wad, const std::string& file) {
    auto entry = wad.lookup(file);
    if (!entry) {
        return std::nullopt;
    }
    auto stream = wad.openEntry(**entry);
    if (!stream) {
        return std::nullopt;
    }
    chunk::ChunkHandlerTable table;
    for (std::uint32_t type = 0; type < chunk::kChunkTypeCount; ++type) {
        if (type != world::kSubtitlesChunk) {
            table.setHandlers(
                type,
                chunk::ChunkHandlers{{},
                                     [](io::Stream& /*chunk*/, const chunk::ChunkHeader& /*header*/,
                                        chunk::ChunkStacks& /*stacks*/) -> std::expected<void, Error> { return {}; }});
        }
    }
    chunk::ChunkStacks stacks;
    if (auto loaded = chunk::loadContainer(*stream, table, stacks); !loaded) {
        return std::nullopt;
    }
    std::vector<chunk::ChunkData> chunks = stacks.takeChunks(world::kSubtitlesChunk);
    if (chunks.empty()) {
        return std::nullopt;
    }
    return std::move(chunks.front().bytes);
}

} // namespace

std::string_view captionLanguageName(Language language) {
    switch (language) {
    case Language::English:
        return "ENGLISH";
    case Language::Spanish:
        return "SPANISH";
    case Language::French:
        return "FRENCH";
    case Language::Italian:
        return "ITALIAN";
    case Language::German:
        return "GERMAN";
    }
    return "ENGLISH";
}

std::optional<std::string> captionLevelFile(std::string_view name) {
    // L<digits>_IN or L<digits>_OUT.
    if (name.size() < 4 || (name[0] != 'L' && name[0] != 'l')) {
        return std::nullopt;
    }
    std::size_t end = 1;
    while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end])) != 0) {
        ++end;
    }
    if (end == 1 || end >= name.size() || name[end] != '_') {
        return std::nullopt;
    }
    return std::format("level{}.lev", name.substr(1, end - 1));
}

CaptionSource wadCaptionSource(const io::Wad& wad, std::function<Language()> language) {
    return [&wad, language = std::move(language)](std::string_view movie) -> std::optional<MovieCaptions> {
        // The timing: <movie>_sub.scn (Movie_Play reads it only when a level's Subtitles chunk is loaded).
        std::string sceneName = std::format("{}_sub", movie);
        for (char& c : sceneName) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const std::optional<std::string> levelFile = captionLevelFile(movie);
        if (!levelFile) {
            return std::nullopt;
        }
        auto record = scenes::wadSceneSource(wad)(sceneName);
        if (!record) {
            return std::nullopt;
        }
        auto scene = scenes::parseSceneHeader(*record);
        if (!scene) {
            return std::nullopt;
        }
        // The text: the level's Subtitles chunk, the language's section, the scene's captions.
        std::optional<std::vector<std::byte>> chunk = readSubtitlesChunk(wad, *levelFile);
        if (!chunk) {
            return std::nullopt;
        }
        auto records = parseSubtitles(*chunk);
        if (!records) {
            return std::nullopt;
        }
        MovieCaptions captions{Captions(std::move(*records), captionLanguageName(language ? language() : Language{})),
                               CaptionTimeline::fromScene(*scene)};
        if (!captions.captions.selectScene(sceneName)) {
            return std::nullopt;
        }
        return captions;
    };
}

} // namespace coney::movies
