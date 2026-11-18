// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "warriors/profile_record.h"
#include "warriors/profile_store.h"

namespace coney {

/// The kinds of original save the importer reads. The PS2's containers today; the Xbox build's save is #1421's, and its
/// reader returns an OriginalSave of a kind of its own.
///
/// Research: docs/research/save.md#import-containers
enum class SaveSourceKind : std::uint8_t {
    Ps2CardImage,  ///< A whole card's image: PCSX2's `.ps2` file (with the pages' spares) or a raw dump (without).
    Ps2FolderCard, ///< PCSX2's folder card: a folder with `_pcsx2_superblock` and a folder per save.
    Ps2SaveFolder, ///< A copy of the save folder (`BASLUS-21215/`), or of a card's root that holds it.
    Ps2DataFile,   ///< The save's data file `BASLUS-21215` alone.
    Ps2Psu,        ///< A single-save export in the PSU format (uLaunchELF, mymc).
};

/// What a source kind is called in the importer's messages ("PCSX2 card image").
/// @coney-only the importer's wording
[[nodiscard]] std::string_view describe(SaveSourceKind kind);

/// One profile found in an original save, with the save system's slot it was in (0-5).
struct ImportCandidate {
    ProfileRecord record;
    std::size_t slot = 0;
};

/// An original save, read: what it was, a name for it in messages (the file or folder name), the profiles it holds in
/// slot order and, for each slot that holds something the game could not load, why it was left out.
struct OriginalSave {
    SaveSourceKind kind = SaveSourceKind::Ps2DataFile;
    std::string source;
    std::vector<ImportCandidate> profiles;
    std::vector<std::string> skipped;
};

/// The NTSC-U game's save folder on the card, and its data file in it (docs/research/save.md#card-files).
inline constexpr std::string_view kPs2SaveName = "BASLUS-21215";
/// The data file's size: six records (`ss + 0x10c`), the only size the game loads (`PS2Save_CheckFileSizes`).
inline constexpr std::size_t kPs2DataFileSize = ProfileStore::kSlots * ProfileRecord::kSize;

/// Reads the profiles of the game's data file, six records of ProfileRecord::kSize. A slot whose name starts with a
/// zero byte is empty; a slot whose version is not ProfileRecord::kVersion is not loaded and named in `skipped`. Fails
/// with ErrorCode::Invalid when the file is not kPs2DataFileSize bytes, which the game calls corrupt.
/// @orig 0x004215d8 SaveSystem_ReadHeaders (W_SaveSystem.cpp)
/// @orig 0x004219d0 ProfileHeader_Read (W_SaveSystem.cpp)
/// @orig 0x0041fcc8 PS2Save_CheckFileSizes (W_PS2SaveSystem.cpp)
[[nodiscard]] std::expected<std::vector<ImportCandidate>, Error> readPs2DataFile(std::span<const std::byte> bytes,
                                                                                 std::vector<std::string>& skipped);

/// Reads an original save from a file's bytes: a card image, a PSU export or the bare data file, told apart by their
/// content. `source` names it in messages. Fails with ErrorCode::Invalid naming the problem: not a save Coney knows, a
/// card or export with no save of the game (naming what it holds instead), a damaged card, a data file of the wrong
/// size, or a save with no profile the game could load.
/// @coney-only the importer: the original reads its own card only
[[nodiscard]] std::expected<OriginalSave, Error> readOriginalSave(std::span<const std::byte> bytes, std::string source);

/// Reads the original save at `path`: a file as readOriginalSave(bytes) does, or a folder (a PCSX2 folder card, a copy
/// of a card's root or of the save folder itself). Fails with ErrorCode::NotFound when there is nothing there or the
/// folder holds no save of the game, and as readOriginalSave(bytes) otherwise. The save is only read, never written.
/// @coney-only the importer: the original reads its own card only
[[nodiscard]] std::expected<OriginalSave, Error> readOriginalSave(const std::filesystem::path& path);

/// Imports `record` as a new profile in the first free slot of `store`, as it is (a Coney save with nothing modded,
/// docs/research/save.md#save-versions), without loading it. Returns the slot. Fails with ErrorCode::NotFound when
/// every slot is used, ErrorCode::Invalid when a profile of the same name exists (PM_Create's check: two profiles never
/// share a name) or the name is empty, and ErrorCode::Io when the store cannot write it.
/// @coney-only the importer's way into the save system
[[nodiscard]] std::expected<std::size_t, Error> importProfile(const ProfileRecord& record, ProfileStore& store);

/// What an import folder holds: the saves read from the files and folders directly in it, and one line for each that
/// could not be read, saying why. Both in name order.
struct ImportScan {
    std::vector<OriginalSave> saves;
    std::vector<std::string> problems;

    /// Every profile found, in order.
    [[nodiscard]] std::size_t profileCount() const;
};

/// Reads every file and folder directly in `folder`, in name order; a missing folder holds nothing.
/// @coney-only the front end's import folder (docs/research/save.md#import)
[[nodiscard]] ImportScan scanImportFolder(const std::filesystem::path& folder);

} // namespace coney
