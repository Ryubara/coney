// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/save_import.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

#include "fileio/ps2_memory_card.h"
#include "fileio/reader.h"

namespace coney {

namespace {

// The marker file PCSX2 keeps at the root of a folder card (docs/research/save.md#import-containers).
constexpr std::string_view kFolderCardMarker = "_pcsx2_superblock";

// Reads a whole file; nothing when it cannot be opened.
// @coney-only the importer's file read
std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

// Whether `bytes` is the size of a card image and starts with an erased page (every byte 0xff): a card never formatted.
// @coney-only the importer's wording
bool looksErased(std::span<const std::byte> bytes) {
    constexpr std::size_t kPage = 512;
    constexpr std::size_t kMegabyte = 1024 * 1024;
    // 8 MB of pages with or without their 16-byte spares, or any whole number of such cards' worth.
    const bool cardSize =
        bytes.size() >= kMegabyte && (bytes.size() % kMegabyte == 0 || bytes.size() % (528 * 2048) == 0);
    return cardSize && std::ranges::all_of(bytes.first(kPage), [](std::byte b) { return b == std::byte{0xff}; });
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

// The OriginalSave of the data file in a save folder: its profiles, or why there are none the game could load.
// @coney-only the importer's container step
std::expected<OriginalSave, Error> fromDataFile(std::span<const std::byte> data, SaveSourceKind kind,
                                                std::string source) {
    OriginalSave save{kind, std::move(source), {}, {}};
    auto profiles = readPs2DataFile(data, save.skipped);
    if (!profiles) {
        return fail(profiles.error().code, std::format("{}: {}", save.source, profiles.error().message));
    }
    if (profiles->empty()) {
        if (!save.skipped.empty()) {
            return fail(ErrorCode::Invalid,
                        std::format("{}: no profile the game could load ({})", save.source, joined(save.skipped)));
        }
        return fail(ErrorCode::Invalid, std::format("{}: the save holds no profile", save.source));
    }
    save.profiles = std::move(*profiles);
    return save;
}

// The OriginalSave of a save folder read from a card or an export: refused when it is another game's or lacks the
// data file.
// @coney-only the importer's container step
std::expected<OriginalSave, Error> fromSaveFolder(const io::Ps2SaveFolder& folder, SaveSourceKind kind,
                                                  std::string source) {
    if (folder.name != kPs2SaveName) {
        return fail(ErrorCode::Invalid, std::format("{}: the {} holds {}, not The Warriors' save {}", source,
                                                    describe(kind), folder.name, kPs2SaveName));
    }
    const io::Ps2SaveFile* data = folder.file(kPs2SaveName);
    if (data == nullptr) {
        return fail(ErrorCode::Invalid,
                    std::format("{}: the save folder {} has no data file {}", source, folder.name, kPs2SaveName));
    }
    return fromDataFile(data->data, kind, std::move(source));
}

// The game's save on a card image: the card read, then its save folder.
// @coney-only the importer's container step
std::expected<OriginalSave, Error> fromCardImage(std::span<const std::byte> bytes, std::string source) {
    const auto card = io::Ps2MemoryCard::parse(bytes);
    if (!card) {
        return fail(ErrorCode::Invalid, std::format("{}: {}", source, card.error().message));
    }
    auto folder = card->folder(kPs2SaveName);
    if (!folder) {
        if (folder.error().code != ErrorCode::NotFound) {
            return fail(ErrorCode::Invalid, std::format("{}: the card is damaged: {}", source, folder.error().message));
        }
        const auto names = card->folders();
        return fail(ErrorCode::Invalid,
                    std::format("{}: the card holds no save of The Warriors ({}); its saves: {}", source, kPs2SaveName,
                                names ? joined(*names) : names.error().message));
    }
    return fromSaveFolder(*folder, SaveSourceKind::Ps2CardImage, std::move(source));
}

// The game's save in a folder: a PCSX2 folder card or a copy of a card's root (both hold `BASLUS-21215/`), or the save
// folder itself (it holds the data file).
// @coney-only the importer's container step
std::expected<OriginalSave, Error> fromFolder(const std::filesystem::path& path) {
    std::error_code ec;
    const std::string source = path.filename().string();
    const bool folderCard = std::filesystem::is_regular_file(path / kFolderCardMarker, ec);
    const SaveSourceKind kind = folderCard ? SaveSourceKind::Ps2FolderCard : SaveSourceKind::Ps2SaveFolder;
    for (const std::filesystem::path& data : {path / kPs2SaveName / kPs2SaveName, path / kPs2SaveName}) {
        if (!std::filesystem::is_regular_file(data, ec)) {
            continue;
        }
        const auto bytes = readFile(data);
        if (!bytes) {
            return fail(ErrorCode::Io, std::format("{}: {} cannot be read", source, data.string()));
        }
        return fromDataFile(*bytes, kind, source);
    }
    // Name what the folder holds instead, so a player who picked the wrong one sees it.
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
        if (entry.is_directory(ec)) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::ranges::sort(names);
    return fail(ErrorCode::NotFound, std::format("{}: the {} holds no save of The Warriors ({}); its folders: {}",
                                                 source, describe(kind), kPs2SaveName, joined(names)));
}

} // namespace

std::string_view describe(SaveSourceKind kind) {
    switch (kind) {
    case SaveSourceKind::Ps2CardImage:
        return "PS2 memory card image";
    case SaveSourceKind::Ps2FolderCard:
        return "PCSX2 folder card";
    case SaveSourceKind::Ps2SaveFolder:
        return "PS2 save folder";
    case SaveSourceKind::Ps2DataFile:
        return "PS2 save file";
    case SaveSourceKind::Ps2Psu:
        return "PSU export";
    }
    return "save";
}

std::expected<std::vector<ImportCandidate>, Error> readPs2DataFile(std::span<const std::byte> bytes,
                                                                   std::vector<std::string>& skipped) {
    // The load step reads the whole image, and any data file of another size is corrupt to the game.
    if (bytes.size() != kPs2DataFileSize) {
        return fail(ErrorCode::Invalid,
                    std::format("the save file is {} bytes; the game's is 7,704 (six profiles of 1,284) and it calls "
                                "any other size corrupt",
                                bytes.size()));
    }
    std::vector<ImportCandidate> profiles;
    for (std::size_t slot = 0; slot < ProfileStore::kSlots; ++slot) {
        const auto record = bytes.subspan(slot * ProfileRecord::kSize, ProfileRecord::kSize);
        io::Reader header(record);
        // The data file is whole, so the header's version word is there.
        const std::uint32_t version = header.readU32Le().value();
        if (version != ProfileRecord::kVersion) {
            // The game leaves such a slot unused and raises its "bad version" flag (`ss + 0x11c`).
            skipped.push_back(std::format("slot {}: version {:#x}, not the game's {:#x}", slot + 1, version,
                                          ProfileRecord::kVersion));
            continue;
        }
        // A slot is used when its name's first byte is not zero.
        if (record[4] == std::byte{0}) {
            continue;
        }
        auto parsed = ProfileRecord::parse(record);
        if (!parsed) {
            skipped.push_back(std::format("slot {}: {}", slot + 1, parsed.error().message));
            continue;
        }
        profiles.push_back({std::move(*parsed), slot});
    }
    return profiles;
}

std::expected<OriginalSave, Error> readOriginalSave(std::span<const std::byte> bytes, std::string source) {
    if (io::Ps2MemoryCard::looksLikeCard(bytes)) {
        return fromCardImage(bytes, std::move(source));
    }
    if (io::looksLikePsu(bytes)) {
        const auto folder = io::readPsuExport(bytes);
        if (!folder) {
            return fail(ErrorCode::Invalid, std::format("{}: {}", source, folder.error().message));
        }
        return fromSaveFolder(*folder, SaveSourceKind::Ps2Psu, std::move(source));
    }
    if (bytes.size() == kPs2DataFileSize) {
        return fromDataFile(bytes, SaveSourceKind::Ps2DataFile, std::move(source));
    }
    // A card that was never formatted is all ones (an erased card's bytes): PCSX2 makes its new card files so.
    if (looksErased(bytes)) {
        return fail(ErrorCode::Invalid,
                    std::format("{}: the memory card is not formatted, so it holds no save", source));
    }
    // A record-sized file whose first word is the record's version is a data file cut short or padded: say so.
    if (bytes.size() >= ProfileRecord::kSize && io::Reader(bytes).readU32Le().value_or(0) == ProfileRecord::kVersion) {
        return fail(ErrorCode::Invalid, std::format("{}: the save file is {} bytes; the game's is 7,704 (six profiles "
                                                    "of 1,284) and it calls any other size corrupt",
                                                    source, bytes.size()));
    }
    return fail(ErrorCode::Invalid, std::format("{}: not a PS2 save Coney can import: neither a memory card image, a "
                                                "PSU export nor the game's 7,704-byte save file {}",
                                                source, kPs2SaveName));
}

std::expected<OriginalSave, Error> readOriginalSave(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return fromFolder(path);
    }
    if (!std::filesystem::is_regular_file(path, ec)) {
        return fail(ErrorCode::NotFound, std::format("{}: no such file or folder", path.string()));
    }
    const auto bytes = readFile(path);
    if (!bytes) {
        return fail(ErrorCode::Io, std::format("{}: cannot be read", path.string()));
    }
    return readOriginalSave(*bytes, path.filename().string());
}

std::expected<std::size_t, Error> importProfile(const ProfileRecord& record, ProfileStore& store) {
    if (record.name.empty()) {
        return fail(ErrorCode::Invalid, "the profile has no name");
    }
    if (store.nameUsed(record.name)) {
        return fail(ErrorCode::Invalid, std::format("a profile named {} exists already", record.name));
    }
    const std::optional<std::size_t> slot = store.freeSlot();
    if (!slot) {
        return fail(ErrorCode::NotFound, std::format("no room for {}: all {} profiles are used; delete one first",
                                                     record.name, ProfileStore::kSlots));
    }
    if (!store.importRecord(*slot, record)) {
        return fail(ErrorCode::Io, std::format("{} could not be written", record.name));
    }
    return *slot;
}

std::size_t ImportScan::profileCount() const {
    std::size_t count = 0;
    for (const OriginalSave& save : saves) {
        count += save.profiles.size();
    }
    return count;
}

ImportScan scanImportFolder(const std::filesystem::path& folder) {
    ImportScan scan;
    std::error_code ec;
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        paths.push_back(entry.path());
    }
    std::ranges::sort(paths);
    for (const std::filesystem::path& path : paths) {
        if (auto save = readOriginalSave(path); save) {
            scan.saves.push_back(std::move(*save));
        } else {
            // Every message names its file already, except a folder or file that could not be opened.
            const std::string& message = save.error().message;
            const std::string name = path.filename().string();
            scan.problems.push_back(message.starts_with(name) ? message : std::format("{}: {}", name, message));
        }
    }
    return scan;
}

} // namespace coney
