// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only Coney's own save container; the field set it carries is Profile_Write's
// (docs/research/save.md#save-format)
#include "warriors/save_game.h"

#include <algorithm>
#include <bit>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>

#include "core/assert.h"
#include "core/name_hash.h"
#include "fileio/reader.h"

namespace coney {

namespace {

// The frame (docs/research/save.md#save-format): the same in every version, so any Coney can read a file's name.
constexpr std::string_view kMagic = "CONEYSAV";
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kTrailerSize = 4;
constexpr std::uint32_t kMaxSections = 1024;
constexpr std::size_t kMaxSectionName = 64;

// The sections of version 1, in the order the writer puts them (docs/research/save.md#save-mapping).
constexpr std::string_view kProfile = "profile";
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

// The longest string the format holds: its length is one byte.
constexpr std::size_t kMaxString = 0xff;

// One framed section: its name and payload.
struct Section {
    std::string name;
    std::vector<std::byte> payload;
};

// A file's frame, read: its version and its sections in file order.
struct Framed {
    std::uint32_t version = 0;
    std::vector<Section> sections;
};

// Appends little-endian values to a growing payload.
class Writer {
  public:
    // One byte.
    void u8(std::uint8_t value) { m_out.push_back(static_cast<std::byte>(value)); }
    // A 32-bit value.
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    // An IEEE 754 single by its bits, so every value (a NaN too) comes back exactly.
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    // A flag: one byte, 0 or 1.
    void flag(bool value) { u8(value ? 1 : 0); }
    // Raw bytes.
    void bytes(std::span<const std::uint8_t> values) {
        for (const std::uint8_t value : values) {
            u8(value);
        }
    }
    // Raw bytes, as std::byte.
    void raw(std::span<const std::byte> values) { m_out.insert(m_out.end(), values.begin(), values.end()); }
    // A string: its length in one byte, then its bytes. The caller keeps it to kMaxString.
    void string(std::string_view text) {
        CONEY_ASSERT(text.size() <= kMaxString);
        u8(static_cast<std::uint8_t>(text.size()));
        for (const char c : text) {
            u8(static_cast<std::uint8_t>(c));
        }
    }
    // The payload written so far.
    [[nodiscard]] std::vector<std::byte> take() { return std::move(m_out); }

  private:
    std::vector<std::byte> m_out;
};

// Reads one section's payload. The first failure is kept and every later read returns zero, so a decoder reads its
// fields straight through and checks once at the end (finish).
class PayloadReader {
  public:
    PayloadReader(std::string_view section, std::span<const std::byte> payload)
        : m_section(section), m_reader(payload) {}

    // One byte.
    std::uint8_t u8() { return take(m_reader.readU8()).value_or(0); }
    // A 32-bit value.
    std::uint32_t u32() { return take(m_reader.readU32Le()).value_or(0); }
    // A single by its bits.
    float f32() { return std::bit_cast<float>(u32()); }
    // A flag; any byte but 0 or 1 is damage.
    bool flag() {
        const std::uint8_t value = u8();
        if (value > 1) {
            invalid(std::format("a flag holds {}", value));
        }
        return value == 1;
    }
    // Raw bytes into `out`.
    void bytes(std::span<std::uint8_t> out) {
        if (auto read = take(m_reader.readBytes(out.size())); read) {
            std::ranges::transform(*read, out.begin(), [](std::byte b) { return static_cast<std::uint8_t>(b); });
        }
    }
    // Raw bytes, as many as asked.
    std::vector<std::byte> raw(std::size_t count) {
        auto read = take(m_reader.readBytes(count));
        return read ? std::vector<std::byte>(read->begin(), read->end()) : std::vector<std::byte>{};
    }
    // A string of at most `longest` bytes and at least `shortest`.
    std::string string(std::size_t shortest, std::size_t longest) {
        const std::size_t length = u8();
        std::string text;
        if (auto read = take(m_reader.readBytes(length)); read) {
            std::ranges::transform(*read, std::back_inserter(text), [](std::byte b) { return static_cast<char>(b); });
        }
        if (length < shortest || length > longest) {
            invalid(std::format("a string of {} bytes, outside {}-{}", length, shortest, longest));
        }
        return text;
    }
    // Bytes left unread.
    [[nodiscard]] std::size_t remaining() const { return m_reader.remaining(); }
    // Whether every read so far was in bounds and within its rules.
    [[nodiscard]] bool ok() const { return !m_error.has_value(); }
    // Notes a value outside its rules.
    void invalid(std::string why) {
        if (!m_error) {
            m_error = Error{ErrorCode::Invalid, std::format("save section {}: {}", m_section, why)};
        }
    }
    // The first failure, or one for bytes left over: a section is exactly its fields.
    [[nodiscard]] std::expected<void, Error> finish() {
        if (!m_error && m_reader.remaining() != 0) {
            invalid(std::format("{} bytes past its fields", m_reader.remaining()));
        }
        if (m_error) {
            return std::unexpected(*m_error);
        }
        return {};
    }

  private:
    // Keeps the first failure of a read; a section that ends early is damage of the section, not of the file.
    template <typename T> std::optional<T> take(std::expected<T, Error> read) {
        if (read) {
            return std::move(*read);
        }
        invalid("ends before its fields");
        return std::nullopt;
    }

    std::string_view m_section;
    io::Reader m_reader;
    std::optional<Error> m_error;
};

// Whether `name` is a section name the frame allows: 1-64 bytes of a-z, 0-9, '-' and ':'.
bool validSectionName(std::string_view name) {
    return !name.empty() && name.size() <= kMaxSectionName && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == ':';
    });
}

// Whether `name` is a mod's name as the manifest allows it: lower case, digits and '-', 1-60 bytes.
bool validModName(std::string_view name) {
    return !name.empty() && name.size() <= ModSection::kMaxName && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}

// Whether `bytes` start with the magic: a framed save of some version, not a bare record.
bool hasMagic(std::span<const std::byte> bytes) {
    return bytes.size() >= kMagic.size() &&
           std::ranges::equal(bytes.first(kMagic.size()), kMagic,
                              [](std::byte b, char c) { return static_cast<char>(b) == c; });
}

// Reads the frame: the header, every section and the trailer. With `checkCrc` false it reads as far as it can for
// nameOf, so a damaged file can still be listed under its name.
std::expected<Framed, Error> readFrame(std::span<const std::byte> bytes, bool checkCrc) {
    if (bytes.size() < kHeaderSize + kTrailerSize) {
        return fail(ErrorCode::Truncated, std::format("a save of {} bytes is shorter than its frame", bytes.size()));
    }
    // The header: the magic, the version and the section count.
    if (!hasMagic(bytes)) {
        return fail(ErrorCode::Invalid, "not a Coney save: no CONEYSAV magic");
    }
    io::Reader reader(bytes.subspan(kMagic.size(), bytes.size() - kMagic.size() - kTrailerSize));
    Framed framed;
    framed.version = reader.readU32Le().value_or(0);
    const std::uint32_t count = reader.readU32Le().value_or(0);
    if (count > kMaxSections) {
        return fail(ErrorCode::Invalid, std::format("a save of {} sections, more than {}", count, kMaxSections));
    }
    // Each section: its name, then its payload's length and the payload.
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto ended = [&] {
            return fail(ErrorCode::Truncated, std::format("the save ends in its section {} of {}", i + 1, count));
        };
        const auto nameLength = reader.readU8();
        if (!nameLength) {
            return ended();
        }
        const auto name = reader.readBytes(*nameLength);
        if (!name) {
            return ended();
        }
        const auto length = reader.readU32Le();
        if (!length) {
            return ended();
        }
        const auto payload = reader.readBytes(*length);
        if (!payload) {
            return ended();
        }
        Section section;
        std::ranges::transform(*name, std::back_inserter(section.name),
                               [](std::byte b) { return static_cast<char>(b); });
        if (!validSectionName(section.name)) {
            return fail(ErrorCode::Invalid, std::format("save section {} has a name outside the rules", i + 1));
        }
        section.payload.assign(payload->begin(), payload->end());
        framed.sections.push_back(std::move(section));
    }
    // The trailer follows the last section at once, and seals every byte before it.
    if (reader.remaining() != 0) {
        return fail(ErrorCode::Invalid,
                    std::format("{} bytes between the last section and the CRC", reader.remaining()));
    }
    if (checkCrc) {
        const std::uint32_t stored = io::loadU32Le(bytes.last(kTrailerSize));
        if (const std::uint32_t crc = crc32(bytes.first(bytes.size() - kTrailerSize)); crc != stored) {
            return fail(ErrorCode::Invalid,
                        std::format("the save's CRC-32 is {:#010x}, its bytes give {:#010x}", stored, crc));
        }
    }
    return framed;
}

// Writes the frame around `sections` in the current version.
std::vector<std::byte> writeFrame(const std::vector<Section>& sections) {
    CONEY_ASSERT(sections.size() <= kMaxSections);
    Writer w;
    for (const char c : kMagic) {
        w.u8(static_cast<std::uint8_t>(c));
    }
    w.u32(SaveGame::kVersion);
    w.u32(static_cast<std::uint32_t>(sections.size()));
    for (const Section& section : sections) {
        CONEY_ASSERT(validSectionName(section.name));
        w.string(section.name);
        w.u32(static_cast<std::uint32_t>(section.payload.size()));
        w.raw(section.payload);
    }
    std::vector<std::byte> out = w.take();
    const std::uint32_t crc = crc32(out);
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>(crc >> shift));
    }
    return out;
}

// The faithful sections of version 1, one field of the record for each field written (save.md#save-mapping).
std::vector<Section> faithfulSections(const ProfileRecord& record) {
    const SavedProgress& p = record.progress;
    std::vector<Section> sections;
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
    return sections;
}

// The story section: its entries sorted by id.
Section storySection(std::vector<MissionProgress> story) {
    std::ranges::sort(story, std::ranges::less{}, &MissionProgress::id);
    Writer w;
    w.u32(static_cast<std::uint32_t>(story.size()));
    for (const MissionProgress& mission : story) {
        CONEY_ASSERT(!mission.id.empty() && mission.id.size() <= kMaxString);
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

// Reads the story section; each id once.
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

// Decodes the sections of the current version into a save.
std::expected<SaveGame, Error> decodeCurrent(const std::vector<Section>& sections) {
    SaveGame save;
    std::set<std::string_view, std::less<>> seen;
    for (const Section& section : sections) {
        const std::string_view name = section.name;
        if (!seen.insert(name).second) {
            return fail(ErrorCode::Invalid, std::format("save section {} twice", name));
        }
        PayloadReader r(name, section.payload);
        SavedProgress& p = save.profile.progress;
        // Each known section into its fields; anything else in this version is damage.
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

// One migration step: the sections of version n + 1 from those of version n (n >= 1). Version 0 has no sections;
// fromRecord takes its place.
using Step = std::expected<std::vector<Section>, Error> (*)(std::vector<Section>);
// The steps from version 1 up, step i taking version i + 1 to i + 2. Empty while version 1 is the current one.
constexpr std::array<Step, SaveGame::kVersion - 1> kSteps{};

// Version 0, the original's bare record, to a save: each field into its section, nothing modded.
std::expected<SaveGame, Error> fromRecord(std::span<const std::byte> bytes) {
    auto record = ProfileRecord::parse(bytes);
    if (!record) {
        return std::unexpected(record.error());
    }
    SaveGame save;
    save.profile = std::move(*record);
    return save;
}

} // namespace

std::vector<std::byte> SaveGame::serialise() const {
    std::vector<Section> sections = faithfulSections(profile);
    if (!modded.story.empty()) {
        sections.push_back(storySection(modded.story));
    }
    std::vector<ModSection> mods = modded.mods;
    std::ranges::sort(mods, std::ranges::less{}, &ModSection::name);
    for (const ModSection& mod : mods) {
        sections.push_back(modSection(mod));
    }
    return writeFrame(sections);
}

std::expected<SaveGame, Error> SaveGame::parse(std::span<const std::byte> bytes) {
    // No magic: version 0, the record earlier Coney builds wrote (and the original's card holds).
    if (!hasMagic(bytes) && bytes.size() == ProfileRecord::kSize) {
        return fromRecord(bytes);
    }
    auto framed = readFrame(bytes, true);
    if (!framed) {
        return std::unexpected(framed.error());
    }
    if (framed->version == 0) {
        return fail(ErrorCode::Invalid, "a framed save cannot be version 0");
    }
    if (framed->version > kVersion) {
        return fail(ErrorCode::Invalid,
                    std::format("the save was made by a newer Coney (version {}; this one reads up to {})",
                                framed->version, kVersion));
    }
    // Step the sections up to the current version, then read them.
    std::vector<Section> sections = std::move(framed->sections);
    for (std::uint32_t version = framed->version; version < kVersion; ++version) {
        auto stepped = kSteps.at(version - 1)(std::move(sections));
        if (!stepped) {
            return std::unexpected(stepped.error());
        }
        sections = std::move(*stepped);
    }
    return decodeCurrent(sections);
}

std::string SaveGame::nameOf(std::span<const std::byte> bytes) {
    auto framed = readFrame(bytes, false);
    if (!framed) {
        // Not framed: a record's header name, as the original lists a slot it cannot load.
        return ProfileRecord::headerName(bytes).value_or(std::string{});
    }
    // The name leads the profile section in every version.
    for (const Section& section : framed->sections) {
        if (section.name == kProfile) {
            PayloadReader r(kProfile, section.payload);
            std::string name = r.string(0, ProfileRecord::kNameLength);
            return r.ok() ? name : std::string{};
        }
    }
    return {};
}

} // namespace coney
