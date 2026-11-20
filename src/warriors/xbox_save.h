// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "fileio/stream.h"
#include "warriors/profile_record.h"
#include "warriors/save_import.h"

namespace coney {

/// The NTSC-U Xbox game's title id in hexadecimal: its folder under `UDATA` (the XBE certificate's title id).
///
/// Research: docs/research/save.md#xbox-save
inline constexpr std::string_view kXboxTitleFolder = "5454009b";
/// The data file in each of the game's save folders.
inline constexpr std::string_view kXboxDataFileName = "profile.dat";
/// The dashboard's file in each save folder, which names the save.
inline constexpr std::string_view kXboxSaveMetaName = "SaveMeta.xbx";
/// The Xbox build's profile record: 1,210 bytes, the PS2's fields in the same order with the options as bytes and no
/// Video setting (SaveSystem_BuildTemplate measures it).
inline constexpr std::size_t kXboxRecordSize = 0x4ba;
/// The signature after the record: an HMAC-SHA1 of it (XCalculateSignature).
inline constexpr std::size_t kXboxSignatureSize = 20;
/// The data file's size, the only one the game's load accepts.
inline constexpr std::size_t kXboxDataFileSize = kXboxRecordSize + kXboxSignatureSize;

/// Reads the Xbox build's profile record (kXboxRecordSize bytes, the signature not included) into the PS2's record,
/// field for field: the options' bytes become the PS2's words and flags, and the Rumble store's 0x30-byte gangs its
/// 0x34-byte ones. The Xbox has no Video setting, so the record's is Normal. Fails with ErrorCode::Invalid when
/// `bytes` is not kXboxRecordSize long or its version is not ProfileRecord::kVersion.
/// @orig xbox 0x0033bcf0 Profile_Read (W_SaveSystem.cpp)
/// @orig xbox 0x0033bc60 ProfileHeader_Read (W_SaveSystem.cpp)
[[nodiscard]] std::expected<ProfileRecord, Error> parseXboxRecord(std::span<const std::byte> bytes);

/// The save's name from a SaveMeta.xbx (UTF-16 text, `Name=` on a line of its own), as ASCII with any other character
/// as `?`; nothing when there is no such line.
/// @coney-only the importer names a save it cannot read by the dashboard's name for it
[[nodiscard]] std::optional<std::string> xboxSaveName(std::span<const std::byte> saveMeta);

/// One of the game's save folders as found in a container: its folder name, and the bytes of its data file and
/// SaveMeta.xbx when they are there. A data file larger than the game's is not read: `data` then holds a buffer of the
/// wrong size and `dataBytes` the file's size, for the message.
struct XboxSaveFiles {
    std::string folder;
    std::optional<std::vector<std::byte>> data;
    std::optional<std::vector<std::byte>> meta;
    std::uint64_t dataBytes = 0; ///< The data file's size on its medium, when known (0: the size of `data`).
};

/// The profiles of the game's save folders, one profile per folder, in the order given, each with its folder's index
/// as its slot. A folder whose data file is missing or not kXboxDataFileSize bytes, or whose profile has no name, is
/// left out and named in `skipped`, as the game's load leaves such a save out (its slot not loadable, or unused). A
/// record of another version is left out and named too, where the game keeps its slot listed under the save's name and
/// raises its "bad version" flag (Coney's choice). Every folder is read, where the game's load stops after six
/// (Coney's choice). The signature is not checked: Coney has no key for it (a stand-in,
/// docs/research/save.md#xbox-import).
/// @orig xbox 0x0033efe0 XboxSave_LoadAll (unknown)
/// @orig xbox 0x0033eda0 XboxSave_ReadSigned (unknown)
[[nodiscard]] std::vector<ImportCandidate> readXboxSaveFolders(const std::vector<XboxSaveFiles>& folders,
                                                               std::vector<std::string>& skipped);

/// Reads the game's saves from a folder on disk, if it holds any: a save folder itself (it holds profile.dat), the
/// title folder `5454009b`, a folder holding it, or one holding `UDATA/5454009b` (a copy of the console's E:).
/// Returns nothing when the folder holds no Xbox save of the game, and fails as readOriginalSave does otherwise.
/// @coney-only the importer's Xbox containers
[[nodiscard]] std::optional<std::expected<OriginalSave, Error>> readXboxFolder(const std::filesystem::path& path);

/// Reads the game's saves from an Xbox disk image in `stream`: xemu's qcow2 image, a raw image of a whole hard disk
/// (the data partition at kXboxDataPartitionOffset) or of one FATX volume (a partition or a memory unit). Returns
/// nothing when the stream is none of these, and fails as readOriginalSave does otherwise.
/// @coney-only the importer's Xbox containers
[[nodiscard]] std::optional<std::expected<OriginalSave, Error>> readXboxImage(io::Stream& stream, std::string source);

} // namespace coney
