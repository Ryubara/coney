// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/anim_catalog.h"

#include <cctype>
#include <map>
#include <utility>

#include "characters/anim_set.h"

namespace coney::debug {

namespace {

// The prefix every anim id's name has.
constexpr std::string_view kNamePrefix = "ANIM_";

// The first word of an `ANIM_*` name with the forms of one word together: the verb's -ING and -ED forms join it.
std::string_view categoryWord(std::string_view word) {
    if (word == "MOUNTING" || word == "MOUNTED") {
        return "MOUNT";
    }
    if (word == "ARRESTING" || word == "ARRESTED") {
        return "ARREST";
    }
    return word;
}

} // namespace

std::string animCategory(std::uint32_t id) {
    std::string_view name = animIdName(id);
    if (!name.starts_with(kNamePrefix) || name.size() == kNamePrefix.size()) {
        return "Other";
    }
    name.remove_prefix(kNamePrefix.size());
    const std::string_view word = categoryWord(name.substr(0, name.find('_')));
    // Title case: the first letter as it is, the rest lowered.
    std::string category(word);
    for (std::size_t i = 1; i < category.size(); ++i) {
        category[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(category[i])));
    }
    return category;
}

bool playableOnPlayer(const anim::AnimClip& clip) { return (clip.boneMask >> anim::kPoseBones) == 0; }

std::vector<AnimClipChoice> clipChoicesOf(const characters::AnimSet& set) {
    std::vector<AnimClipChoice> choices;
    for (std::uint32_t id = 0; id < kAnimIdCount; ++id) {
        const anim::AnimClip* clip = set.clip(id);
        if (clip == nullptr || !playableOnPlayer(*clip)) {
            continue;
        }
        choices.push_back(AnimClipChoice{.id = id,
                                         .clip = clip->name,
                                         .seconds = clip->duration,
                                         .rate = set.rate(id),
                                         .generic = set.fromDefaults(id)});
    }
    return choices;
}

std::vector<AnimCategoryClips> byCategory(const std::vector<AnimClipChoice>& clips) {
    // A map keeps the categories sorted; the clips go in as they come, in id order when `clips` is.
    std::map<std::string, std::vector<AnimClipChoice>, std::less<>> grouped;
    for (const AnimClipChoice& clip : clips) {
        grouped[animCategory(clip.id)].push_back(clip);
    }
    std::vector<AnimCategoryClips> categories;
    categories.reserve(grouped.size());
    for (auto& [name, members] : grouped) {
        categories.push_back(AnimCategoryClips{.name = name, .clips = std::move(members)});
    }
    return categories;
}

} // namespace coney::debug
