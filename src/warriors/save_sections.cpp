// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only Coney's own save container: the current version's section codecs; the field set they carry is
// Profile_Write's (docs/research/save.md#save-mapping)
#include "warriors/save_sections.h"

#include <array>
#include <functional>
#include <ranges>
#include <set>

namespace coney::save_sections {

namespace {

// The sections of version 1, in the order the writer puts them (docs/research/save.md#save-mapping).
constexpr std::string_view kMissions = "missions";
constexpr std::string_view kUnlockables = "unlockables";
constexpr std::string_view kRumble = "rumble";
constexpr std::string_view kBank = "bank";
constexpr std::string_view kOptions = "options";
constexpr std::string_view kScriptFlags = "script-flags";
constexpr std::string_view kStory = "story";
constexpr std::string_view kModPrefix = "mod:";
constexpr std::array<std::string_view, 7> kFaithful{kProfile, kMissions, kUnlockables, kRumble,
                                                    kBank,    kOptions,  kScriptFlags};

// Whether `name` is a mod's name as the manifest allows it: lower case, digits and '-', 1-60 bytes.
bool validModName(std::string_view name) {
    return !name.empty() && name.size() <= ModSection::kMaxName && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}

// Whether `id` is a mod mission's id, `<mod>.<id>` (mission-mods.md#graph): a mod's name, a '.', and the mission's
// own id of lower case, digits, '-' and '_', at most kMaxString bytes in all.
bool validStoryId(std::string_view id) {
    const std::size_t dot = id.find('.');
    if (dot == std::string_view::npos || id.size() > kMaxString || !validModName(id.substr(0, dot))) {
        return false;
    }
    const std::string_view own = id.substr(dot + 1);
    return !own.empty() && std::ranges::all_of(own, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

// The faithful sections of version 1, one field of the record for each field written (save.md#save-mapping).
void encodeFaithful(const ProfileRecord& record, std::vector<Section>& sections) {
    const SavedProgress& p = record.progress;
    // The name (cut to the record's eight bytes), the fourth-difficulty unlock, the difficulty, the game-state word.
    Writer profile;
    profile.string(std::string_view(record.name).substr(0, ProfileRecord::kNameLength));
    profile.flag(record.hardcoreFinished);
    profile.u8(record.difficulty);
    profile.u32(p.stateWord0);
    sections.push_back({std::string(kProfile), profile.take()});
    // The 30 best records.
    Writer missions;
    missions.u8(static_cast<std::uint8_t>(SavedProgress::kMissions));
    for (const MissionBest& best : p.missionBests) {
        missions.u32(best.bestScore);
        missions.u32(best.total);
        missions.u8(best.grade);
        missions.u8(best.percent);
        missions.bytes(best.rest);
    }
    sections.push_back({std::string(kMissions), missions.take()});
    // The two unlockable sets, then the Rumble store and its packed bits.
    Writer unlockables;
    unlockables.bytes(p.lockedBits);
    unlockables.bytes(p.newBits);
    sections.push_back({std::string(kUnlockables), unlockables.take()});
    Writer rumble;
    rumble.bytes(p.rumbleData);
    rumble.bytes(p.rumbleBits);
    sections.push_back({std::string(kRumble), rumble.take()});
    Writer bank;
    bank.u32(p.bankedMoney);
    sections.push_back({std::string(kBank), bank.take()});
    // The option block, the per-pad options and the global ones, in the record's order.
    Writer options;
    options.u32(p.optionWord0);
    options.u8(record.brightness);
    options.f32(p.soundFxVolume);
    options.f32(p.musicVolume);
    options.u32(p.optionWord10);
    for (const PadOptions& pad : p.pads) {
        options.flag(pad.invertCamera);
        options.flag(pad.autoAdjustCamera);
        options.flag(pad.vibration);
    }
    options.flag(p.splitScreenMerge);
    options.flag(record.subtitles);
    options.flag(p.proLogic2);
    options.flag(p.wideVideo);
    sections.push_back({std::string(kOptions), options.take()});
    Writer flags;
    flags.bytes(p.scriptFlags);
    sections.push_back({std::string(kScriptFlags), flags.take()});
}

// The story section: its entries sorted by id.
Section storySection(std::vector<MissionProgress> story) {
    std::ranges::sort(story, std::ranges::less{}, &MissionProgress::id);
    Writer w;
    w.u32(static_cast<std::uint32_t>(story.size()));
    for (const MissionProgress& mission : story) {
        CONEY_ASSERT(validStoryId(mission.id));
        w.string(mission.id);
        w.flag(mission.complete);
        w.u32(mission.ranks);
        w.u32(mission.bestScore);
        w.u32(mission.total);
        w.u8(mission.grade);
        w.u8(mission.percent);
    }
    return {std::string(kStory), w.take()};
}

// One mod's section.
Section modSection(const ModSection& mod) {
    CONEY_ASSERT(validModName(mod.name) && mod.version.size() <= kMaxString);
    Writer w;
    w.string(mod.version);
    w.bytes(mod.packageHash);
    w.u32(static_cast<std::uint32_t>(mod.data.size()));
    w.raw(mod.data);
    return {std::string(kModPrefix) + mod.name, w.take()};
}

// Reads the profile section into the record.
void readProfile(PayloadReader& r, ProfileRecord& record) {
    record.name = r.string(0, ProfileRecord::kNameLength);
    record.hardcoreFinished = r.flag();
    record.difficulty = r.u8();
    record.progress.stateWord0 = r.u32();
}

// Reads the 30 best records.
void readMissions(PayloadReader& r, SavedProgress& p) {
    if (const std::uint8_t count = r.u8(); count != SavedProgress::kMissions) {
        r.invalid(std::format("{} missions, not {}", count, SavedProgress::kMissions));
        return;
    }
    for (MissionBest& best : p.missionBests) {
        best.bestScore = r.u32();
        best.total = r.u32();
        best.grade = r.u8();
        best.percent = r.u8();
        r.bytes(best.rest);
    }
}

// Reads the option block and the per-pad and global options.
void readOptions(PayloadReader& r, ProfileRecord& record) {
    SavedProgress& p = record.progress;
    p.optionWord0 = r.u32();
    record.brightness = r.u8();
    p.soundFxVolume = r.f32();
    p.musicVolume = r.f32();
    p.optionWord10 = r.u32();
    for (PadOptions& pad : p.pads) {
        pad.invertCamera = r.flag();
        pad.autoAdjustCamera = r.flag();
        pad.vibration = r.flag();
    }
    p.splitScreenMerge = r.flag();
    record.subtitles = r.flag();
    p.proLogic2 = r.flag();
    p.wideVideo = r.flag();
}

// Reads the story section; each id once, each of the form `<mod>.<id>`.
void readStory(PayloadReader& r, std::vector<MissionProgress>& story) {
    const std::uint32_t count = r.u32();
    // The smallest entry is 17 bytes, so a count the payload cannot hold is damage, found before reserving for it.
    constexpr std::size_t kSmallestEntry = 1 + 1 + 1 + 4 + 4 + 4 + 1 + 1;
    if (count > r.remaining() / kSmallestEntry) {
        r.invalid(std::format("{} missions in {} bytes", count, r.remaining()));
        return;
    }
    std::set<std::string, std::less<>> seen;
    for (std::uint32_t i = 0; i < count; ++i) {
        MissionProgress mission;
        mission.id = r.string(1, kMaxString);
        mission.complete = r.flag();
        mission.ranks = r.u32();
        mission.bestScore = r.u32();
        mission.total = r.u32();
        mission.grade = r.u8();
        mission.percent = r.u8();
        if (r.ok() && !validStoryId(mission.id)) {
            r.invalid(std::format("mission id {} is not <mod>.<id>", mission.id));
        }
        if (!seen.insert(mission.id).second) {
            r.invalid(std::format("mission {} twice", mission.id));
        }
        story.push_back(std::move(mission));
    }
}

// Reads one mod's section.
void readMod(PayloadReader& r, ModSection& mod) {
    mod.version = r.string(0, kMaxString);
    r.bytes(mod.packageHash);
    const std::uint32_t length = r.u32();
    if (length != r.remaining()) {
        r.invalid(std::format("its data says {} bytes, {} are there", length, r.remaining()));
        return;
    }
    mod.data = r.raw(length);
}

// Reads one section of the current version into `save`; false for a name this version does not have.
bool readSection(std::string_view name, PayloadReader& r, SaveGame& save) {
    SavedProgress& p = save.profile.progress;
    if (name == kProfile) {
        readProfile(r, save.profile);
    } else if (name == kMissions) {
        readMissions(r, p);
    } else if (name == kUnlockables) {
        r.bytes(p.lockedBits);
        r.bytes(p.newBits);
    } else if (name == kRumble) {
        r.bytes(p.rumbleData);
        r.bytes(p.rumbleBits);
    } else if (name == kBank) {
        p.bankedMoney = r.u32();
    } else if (name == kOptions) {
        readOptions(r, save.profile);
    } else if (name == kScriptFlags) {
        r.bytes(p.scriptFlags);
    } else if (name == kStory) {
        readStory(r, save.modded.story);
    } else if (name.starts_with(kModPrefix) && validModName(name.substr(kModPrefix.size()))) {
        ModSection mod;
        mod.name = std::string(name.substr(kModPrefix.size()));
        readMod(r, mod);
        save.modded.mods.push_back(std::move(mod));
    } else {
        return false;
    }
    return true;
}

} // namespace

std::vector<Section> encode(const SaveGame& save) {
    std::vector<Section> sections;
    encodeFaithful(save.profile, sections);
    if (!save.modded.story.empty()) {
        sections.push_back(storySection(save.modded.story));
    }
    std::vector<ModSection> mods = save.modded.mods;
    std::ranges::sort(mods, std::ranges::less{}, &ModSection::name);
    for (const ModSection& mod : mods) {
        sections.push_back(modSection(mod));
    }
    return sections;
}

std::expected<SaveGame, Error> decode(const std::vector<Section>& sections) {
    SaveGame save;
    std::set<std::string_view, std::less<>> seen;
    for (const Section& section : sections) {
        const std::string_view name = section.name;
        if (!seen.insert(name).second) {
            return fail(ErrorCode::Invalid, std::format("save section {} twice", name));
        }
        // Each known section into its fields; anything else in this version is damage.
        PayloadReader r(name, section.payload);
        if (!readSection(name, r, save)) {
            return fail(ErrorCode::Invalid,
                        std::format("save section {} is not one of version {}", name, SaveGame::kVersion));
        }
        if (auto read = r.finish(); !read) {
            return std::unexpected(read.error());
        }
    }
    for (const std::string_view name : kFaithful) {
        if (!seen.contains(name)) {
            return fail(ErrorCode::Invalid, std::format("the save has no {} section", name));
        }
    }
    return save;
}

std::string profileName(const Section& profile) {
    PayloadReader r(kProfile, profile.payload);
    std::string name = r.string(0, ProfileRecord::kNameLength);
    return r.ok() ? name : std::string{};
}

} // namespace coney::save_sections
