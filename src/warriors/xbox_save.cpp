// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/xbox_save.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <system_error>
#include <utility>

#include "core/assert.h"
#include "fileio/fatx_volume.h"
#include "fileio/file_stream.h"
#include "fileio/qcow2_image.h"
#include "fileio/reader.h"

namespace coney {

namespace {

// The Xbox Rumble store (0x22c bytes) and the PS2's (0x254): the 512 owned bits, then ten gangs of 0x30 bytes on the
// Xbox and 0x34 on the PS2, then the current gang, the current member and the "changed" mark
// (docs/research/save.md#xbox-record, frontend.md#rumble-gangs).
constexpr std::size_t kXboxRumbleBytes = 0x22c;
constexpr std::size_t kRumbleOwnedBytes = 0x40;
constexpr std::size_t kRumbleGangs = 10;
constexpr std::size_t kXboxGangBytes = 0x30;
constexpr std::size_t kPs2GangBytes = 0x34;
constexpr std::size_t kGangNameLength = 10;
constexpr std::size_t kGangMembers = 9;
constexpr std::size_t kXboxGangName = 0x01;
constexpr std::size_t kXboxGangMembers = 0x0c;
constexpr std::size_t kPs2GangName = 0x04;
constexpr std::size_t kPs2GangMembers = 0x10;
constexpr std::size_t kXboxRumbleTail = kRumbleOwnedBytes + kRumbleGangs * kXboxGangBytes; // 0x220
constexpr std::size_t kPs2RumbleTail = kRumbleOwnedBytes + kRumbleGangs * kPs2GangBytes;   // 0x248
static_assert(kXboxRumbleTail + 12 == kXboxRumbleBytes);
static_assert(kPs2RumbleTail + 12 == SavedProgress::kRumbleBytes);
// The largest SaveMeta.xbx the importer reads: a name and a few lines of UTF-16.
constexpr std::uint32_t kSaveMetaLimit = 64 * 1024;

// Writes `value` as a little-endian word at `offset` of the PS2 store.
// @coney-only the importer writes the Xbox fields into the PS2 store Coney keeps
void putWord(std::span<std::uint8_t> store, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        store[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
    }
}

// The PS2 store of the Xbox store `xbox`: each field moved to its PS2 place and widened to the PS2's word.
// @orig xbox 0x0012c840 RumbleGangs_Read (unknown)
// @orig xbox 0x0012c330 RumbleGangs_CreateGang (unknown)
std::array<std::uint8_t, SavedProgress::kRumbleBytes> ps2RumbleStore(std::span<const std::byte> xbox) {
    std::array<std::uint8_t, SavedProgress::kRumbleBytes> ps2{};
    const auto byteAt = [&xbox](std::size_t offset) { return static_cast<std::uint8_t>(xbox[offset]); };
    // The owned bits are the same 512 bits on both.
    for (std::size_t i = 0; i < kRumbleOwnedBytes; ++i) {
        ps2[i] = byteAt(i);
    }
    // Each gang: the in-use byte becomes a word, the ten-character name moves from +1 to +4 (its terminator stays
    // zero), the nine member words from +0x0c to +0x10.
    for (std::size_t gang = 0; gang < kRumbleGangs; ++gang) {
        const std::size_t from = kRumbleOwnedBytes + gang * kXboxGangBytes;
        const std::size_t to = kRumbleOwnedBytes + gang * kPs2GangBytes;
        putWord(ps2, to, byteAt(from));
        for (std::size_t i = 0; i < kGangNameLength; ++i) {
            ps2[to + kPs2GangName + i] = byteAt(from + kXboxGangName + i);
        }
        for (std::size_t m = 0; m < kGangMembers; ++m) {
            putWord(ps2, to + kPs2GangMembers + 4 * m, io::loadU32Le(xbox.subspan(from + kXboxGangMembers + 4 * m, 4)));
        }
    }
    // The current gang and member are words on both; "changed" is a byte on the Xbox and a word on the PS2.
    putWord(ps2, kPs2RumbleTail, io::loadU32Le(xbox.subspan(kXboxRumbleTail, 4)));
    putWord(ps2, kPs2RumbleTail + 4, io::loadU32Le(xbox.subspan(kXboxRumbleTail + 4, 4)));
    putWord(ps2, kPs2RumbleTail + 8, byteAt(kXboxRumbleTail + 8));
    return ps2;
}

// The name in a record's eight name bytes: up to the first zero byte.
// @coney-only the importer's copy of the record's name
std::string nameOf(std::span<const std::byte> bytes) {
    std::string name;
    for (const std::byte b : bytes) {
        if (b == std::byte{0}) {
            break;
        }
        name.push_back(static_cast<char>(b));
    }
    return name;
}

// Whether two names are the same ignoring case, as FATX and the console's folders compare them.
// @coney-only the importer's name match; the console's file system does it for the game
bool sameName(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

// The names of a list, comma-separated, for a message.
// @coney-only the importer's wording
std::string joined(const std::vector<std::string>& names) {
    std::string text;
    for (const std::string& name : names) {
        text += text.empty() ? name : ", " + name;
    }
    return text.empty() ? "nothing" : text;
}

// The OriginalSave of the save folders found in a container: their profiles, or why there are none.
// @coney-only the importer's Xbox containers
std::expected<OriginalSave, Error> fromFolders(const std::vector<XboxSaveFiles>& folders, SaveSourceKind kind,
                                               std::string source) {
    OriginalSave save{kind, std::move(source), {}, {}, {}};
    if (folders.empty()) {
        return fail(ErrorCode::Invalid,
                    std::format("{}: the Xbox title folder {} holds no save", save.source, kXboxTitleFolder));
    }
    save.profiles = readXboxSaveFolders(folders, save.skipped);
    for (const XboxSaveFiles& folder : folders) {
        save.folders.push_back(folder.folder);
    }
    if (save.profiles.empty()) {
        return fail(ErrorCode::Invalid,
                    std::format("{}: no profile the game could load ({})", save.source, joined(save.skipped)));
    }
    return save;
}

// Reads a file of at most `limit` bytes from disk, or nothing when it cannot be read.
// @coney-only the importer's file read
std::optional<std::vector<std::byte>> readSmallFile(const std::filesystem::path& path, std::uint64_t limit) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > limit) {
        return std::nullopt;
    }
    auto stream = io::FileStream::open(path, 0, size);
    if (!stream) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!stream->read(bytes)) {
        return std::nullopt;
    }
    return bytes;
}

// The entry of `folder` on disk called `name` ignoring case, or none.
// @coney-only the importer's Xbox containers
std::optional<std::filesystem::path> childNamed(const std::filesystem::path& folder, std::string_view name) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (sameName(entry.path().filename().string(), name)) {
            return entry.path();
        }
    }
    return std::nullopt;
}

// The files of one save folder on disk (its data file and SaveMeta.xbx, either or both possibly missing). A data file
// larger than the game's is not read: a buffer of the wrong size stands for it, so the folder is named as skipped.
// @coney-only the importer's Xbox containers
XboxSaveFiles filesOnDisk(const std::filesystem::path& folder) {
    XboxSaveFiles files{folder.filename().string(), std::nullopt, std::nullopt, 0};
    if (const auto data = childNamed(folder, kXboxDataFileName)) {
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(*data, ec);
        files.data = size > kXboxDataFileSize
                         ? std::vector<std::byte>(kXboxDataFileSize + 1)
                         : readSmallFile(*data, kXboxDataFileSize).value_or(std::vector<std::byte>{});
        files.dataBytes = ec ? 0 : size;
    }
    if (const auto meta = childNamed(folder, kXboxSaveMetaName)) {
        files.meta = readSmallFile(*meta, kSaveMetaLimit);
    }
    return files;
}

// The save folders of the title folder `title` on disk, in name order: each sub-folder holding a data file or
// SaveMeta.xbx.
// @coney-only the importer's Xbox containers
std::vector<XboxSaveFiles> saveFoldersOnDisk(const std::filesystem::path& title) {
    std::vector<std::filesystem::path> folders;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(title, ec)) {
        if (entry.is_directory(ec) &&
            (childNamed(entry.path(), kXboxDataFileName) || childNamed(entry.path(), kXboxSaveMetaName))) {
            folders.push_back(entry.path());
        }
    }
    std::ranges::sort(folders);
    std::vector<XboxSaveFiles> saves;
    saves.reserve(folders.size()); // the size is known, so the loop below never regrows the vector
    for (const std::filesystem::path& folder : folders) {
        saves.push_back(filesOnDisk(folder));
    }
    return saves;
}

// The entry called `name` of `entries` ignoring case, or none.
// @coney-only the importer's Xbox containers
std::optional<io::FatxEntry> entryNamed(const std::vector<io::FatxEntry>& entries, std::string_view name) {
    for (const io::FatxEntry& entry : entries) {
        if (sameName(entry.name, name)) {
            return entry;
        }
    }
    return std::nullopt;
}

// The game's saves on a FATX volume (the data partition or a memory unit): `UDATA/5454009b/<save>/` at its root.
// @coney-only the importer's Xbox containers
std::expected<OriginalSave, Error> fromVolume(io::Stream& stream, std::uint64_t offset, std::uint64_t size,
                                              SaveSourceKind kind, std::string source) {
    const auto volume = io::FatxVolume::open(stream, offset, size);
    if (!volume) {
        return fail(ErrorCode::Invalid, std::format("{}: {}", source, volume.error().message));
    }
    const auto root = volume->root();
    if (!root) {
        return fail(ErrorCode::Invalid, std::format("{}: {}", source, root.error().message));
    }
    const auto udata = entryNamed(*root, "UDATA");
    std::vector<io::FatxEntry> titles;
    if (udata && udata->directory) {
        auto listed = volume->list(*udata);
        if (!listed) {
            return fail(ErrorCode::Invalid, std::format("{}: {}", source, listed.error().message));
        }
        titles = std::move(*listed);
    }
    const auto title = entryNamed(titles, kXboxTitleFolder);
    if (!title || !title->directory) {
        std::vector<std::string> names;
        names.reserve(titles.size()); // one name a title, known before the loop
        for (const io::FatxEntry& entry : titles) {
            names.push_back(entry.name);
        }
        return fail(ErrorCode::Invalid,
                    std::format("{}: the {} holds no save of The Warriors (UDATA/{}); its saves are of titles: {}",
                                source, describe(kind), kXboxTitleFolder, joined(names)));
    }
    auto entries = volume->list(*title);
    if (!entries) {
        return fail(ErrorCode::Invalid, std::format("{}: {}", source, entries.error().message));
    }
    std::ranges::sort(*entries, {}, &io::FatxEntry::name);
    std::vector<XboxSaveFiles> folders;
    for (const io::FatxEntry& entry : *entries) {
        if (!entry.directory) {
            continue;
        }
        const auto files = volume->list(entry);
        if (!files) {
            return fail(ErrorCode::Invalid, std::format("{}: {}", source, files.error().message));
        }
        const auto data = entryNamed(*files, kXboxDataFileName);
        const auto meta = entryNamed(*files, kXboxSaveMetaName);
        if (!data && !meta) {
            continue;
        }
        XboxSaveFiles save{entry.name, std::nullopt, std::nullopt, 0};
        if (data && data->size > kXboxDataFileSize) {
            // Larger than the game's: not read, and a buffer of the wrong size has it left out and named.
            save.data = std::vector<std::byte>(kXboxDataFileSize + 1);
            save.dataBytes = data->size;
        } else if (data) {
            auto bytes = volume->read(*data, static_cast<std::uint32_t>(kXboxDataFileSize));
            if (!bytes) {
                return fail(ErrorCode::Invalid, std::format("{}: {}", source, bytes.error().message));
            }
            save.data = std::move(*bytes);
        }
        if (meta) {
            if (auto bytes = volume->read(*meta, kSaveMetaLimit)) {
                save.meta = std::move(*bytes);
            }
        }
        folders.push_back(std::move(save));
    }
    return fromFolders(folders, kind, std::move(source));
}

} // namespace

std::expected<ProfileRecord, Error> parseXboxRecord(std::span<const std::byte> bytes) {
    if (bytes.size() != kXboxRecordSize) {
        return fail(ErrorCode::Invalid,
                    std::format("the profile record is {} bytes; the Xbox game's is 1,210", bytes.size()));
    }
    io::Reader r(bytes);
    // Every read below is in bounds (the size was checked), so the values are taken without further tests.
    const auto u32 = [&r] { return r.readU32Le().value_or(0); };
    const auto f32 = [&r] { return r.readF32Le().value_or(0.0F); };
    const auto u8 = [&r] { return r.readU8().value_or(0); };
    const auto flag = [&u8] { return u8() != 0; };
    const auto raw = [&r](std::size_t count) { return r.readBytes(count).value_or(std::span<const std::byte>{}); };

    // The header (ProfileHeader_Read): the version, the name, the fourth-difficulty unlock as one byte.
    if (const std::uint32_t version = u32(); version != ProfileRecord::kVersion) {
        return fail(ErrorCode::Invalid,
                    std::format("profile record version {:#x}, expected {:#x}", version, ProfileRecord::kVersion));
    }
    ProfileRecord record;
    record.name = nameOf(raw(ProfileRecord::kNameLength));
    record.hardcoreFinished = flag();
    // The per-mission bests, the two unlockable sets, the Rumble store and its packed bits, the money: as the PS2's.
    SavedProgress& p = record.progress;
    for (MissionBest& best : p.missionBests) {
        best.bestScore = u32();
        best.total = u32();
        best.grade = u8();
        best.percent = u8();
        best.rest = {u8(), u8()};
    }
    for (std::uint8_t& b : p.lockedBits) {
        b = u8();
    }
    for (std::uint8_t& b : p.newBits) {
        b = u8();
    }
    p.rumbleData = ps2RumbleStore(raw(kXboxRumbleBytes));
    for (std::uint8_t& b : p.rumbleBits) {
        b = u8();
    }
    p.bankedMoney = u32();
    // The option block, laid out as the PS2's: a word, the brightness byte and three more, two volumes, a word.
    p.optionWord0 = u32();
    record.brightness = u8();
    raw(3);
    p.soundFxVolume = f32();
    p.musicVolume = f32();
    p.optionWord10 = u32();
    // The options the PS2 keeps in words, one byte each here; the Video setting is not saved (Normal).
    for (PadOptions& pad : p.pads) {
        pad.invertCamera = flag();
        pad.autoAdjustCamera = flag();
        pad.vibration = flag();
    }
    p.splitScreenMerge = flag();
    record.subtitles = flag();
    p.proLogic2 = flag();
    p.wideVideo = false;
    record.difficulty = static_cast<std::uint8_t>(u32());
    p.stateWord0 = u32();
    for (std::uint8_t& b : p.scriptFlags) {
        b = u8();
    }
    CONEY_ASSERT(r.remaining() == 0);
    return record;
}

std::optional<std::string> xboxSaveName(std::span<const std::byte> saveMeta) {
    // UTF-16, little-endian, with or without its byte-order mark; lines end in CR LF.
    std::string text;
    for (std::size_t i = 0; i + 1 < saveMeta.size(); i += 2) {
        const auto unit = static_cast<std::uint16_t>(static_cast<std::uint16_t>(saveMeta[i]) |
                                                     (static_cast<std::uint16_t>(saveMeta[i + 1]) << 8U));
        if (unit == 0xfeff && i == 0) {
            continue;
        }
        text.push_back(unit < 0x80 ? static_cast<char>(unit) : '?');
    }
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.starts_with("Name=")) {
            return line.substr(5);
        }
        at = end + 1;
    }
    return std::nullopt;
}

std::vector<ImportCandidate> readXboxSaveFolders(const std::vector<XboxSaveFiles>& folders,
                                                 std::vector<std::string>& skipped) {
    std::vector<ImportCandidate> profiles;
    // Coney's choice: every save folder is read, where the game's load (Xbox 0x0033efe0) stops after six, so a
    // player with more saves than the game lists can still bring each over.
    for (std::size_t index = 0; index < folders.size(); ++index) {
        const XboxSaveFiles& folder = folders[index];
        // The save is named by its dashboard name when it has one, so a player sees which save was left out.
        const std::optional<std::string> saveName = folder.meta ? xboxSaveName(*folder.meta) : std::nullopt;
        const std::string place =
            folder.folder.empty() ? std::string(kXboxDataFileName) : std::format("save folder {}", folder.folder);
        const std::string label = saveName ? std::format("save {} ({})", *saveName, place) : place;
        if (!folder.data) {
            skipped.push_back(std::format("{}: no {}", label, kXboxDataFileName));
            continue;
        }
        // The game's load opens only a data file of the record's length and its 20-byte signature, and marks any other
        // save not loadable. Stand-in: the signature is not checked (Coney has no key for it).
        if (folder.data->size() != kXboxDataFileSize) {
            skipped.push_back(std::format("{}: {} is {} bytes; the game's is 1,230 (a profile of 1,210 and its "
                                          "20-byte signature) and it cannot load any other",
                                          label, kXboxDataFileName,
                                          std::max<std::uint64_t>(folder.dataBytes, folder.data->size())));
            continue;
        }
        // Coney's choice: a record of another version is left out and named. The game's header read (Xbox
        // 0x0033bc60) instead raises its "bad version" flag (+0xc8) and keeps the slot listed under the save's name;
        // the importer has no such slot to show, and nothing it could import from a record it cannot read.
        auto record = parseXboxRecord(std::span(*folder.data).first(kXboxRecordSize));
        if (!record) {
            skipped.push_back(std::format("{}: {}", label, record.error().message));
            continue;
        }
        // ProfileHeader_Read: a slot is used only when its name's first byte is not zero.
        if (record->name.empty()) {
            skipped.push_back(std::format("{}: the profile has no name, so the game lists none", label));
            continue;
        }
        profiles.push_back({std::move(*record), index});
    }
    return profiles;
}

std::optional<std::expected<OriginalSave, Error>> readXboxFolder(const std::filesystem::path& path) {
    const std::string source = path.filename().string();
    // A save folder itself.
    if (childNamed(path, kXboxDataFileName) || childNamed(path, kXboxSaveMetaName)) {
        return fromFolders({filesOnDisk(path)}, SaveSourceKind::XboxSaveFolder, source);
    }
    // The title folder, or a folder holding it, or UDATA above it.
    std::optional<std::filesystem::path> title;
    if (sameName(source, kXboxTitleFolder)) {
        title = path;
    } else if (auto child = childNamed(path, kXboxTitleFolder)) {
        title = child;
    } else if (auto udata = childNamed(path, "UDATA")) {
        title = childNamed(*udata, kXboxTitleFolder);
    }
    if (!title) {
        return std::nullopt;
    }
    return fromFolders(saveFoldersOnDisk(*title), SaveSourceKind::XboxSaveFolder, source);
}

std::optional<std::expected<OriginalSave, Error>> readXboxImage(io::Stream& stream, std::string source) {
    if (io::Qcow2Image::looksLikeQcow2(stream)) {
        auto disk = io::Qcow2Image::open(stream);
        if (!disk) {
            return fail(ErrorCode::Invalid, std::format("{}: {}", source, disk.error().message));
        }
        if (disk->size() < io::kXboxDataPartitionOffset + io::kXboxDataPartitionSize ||
            !io::FatxVolume::looksLikeFatx(*disk, io::kXboxDataPartitionOffset)) {
            return fail(ErrorCode::Invalid, std::format("{}: the disk image holds no Xbox data partition (no FATX "
                                                        "volume at {:#x})",
                                                        source, io::kXboxDataPartitionOffset));
        }
        return fromVolume(*disk, io::kXboxDataPartitionOffset, io::kXboxDataPartitionSize,
                          SaveSourceKind::XboxQcow2Image, std::move(source));
    }
    if (stream.size() >= io::kXboxDataPartitionOffset + io::kXboxDataPartitionSize &&
        io::FatxVolume::looksLikeFatx(stream, io::kXboxDataPartitionOffset)) {
        return fromVolume(stream, io::kXboxDataPartitionOffset, io::kXboxDataPartitionSize,
                          SaveSourceKind::XboxHddImage, std::move(source));
    }
    if (io::FatxVolume::looksLikeFatx(stream, 0)) {
        return fromVolume(stream, 0, stream.size(), SaveSourceKind::XboxFatxImage, std::move(source));
    }
    return std::nullopt;
}

} // namespace coney
