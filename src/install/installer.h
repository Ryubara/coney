// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "fileio/disc.h"
#include "install/install_folder.h"
#include "install/known_discs.h"

namespace coney::install {

/// The folder the PS2 disc's files go into (docs/guides/asset-and-script-architecture.md#install-folder).
inline constexpr std::string_view kPs2Folder = "game/ps2";
/// The folder of the install's own records.
inline constexpr std::string_view kIndexFolder = "index";
/// The record of a finished install; its absence means the install has not finished.
inline constexpr std::string_view kManifestPath = "index/install.json";
/// The empty folders an install makes for mods and texture packs.
inline constexpr std::array<std::string_view, 6> kOverrideFolders{"overrides/packs",        "overrides/textures",
                                                                  "overrides/audio/sounds", "overrides/audio/music",
                                                                  "overrides/movies",       "overrides/files"};
/// The suffix a file has while it is copied; it is renamed once its SHA-1 has been checked.
inline constexpr std::string_view kPartSuffix = ".part";

/// The Coney version this build reports, from the project's version in CMakeLists.txt.
[[nodiscard]] std::string_view coneyVersion();

/// One file a source disc offers: its name as the disc spells it and its size.
struct SourceFile {
    std::string name;
    std::uint64_t size = 0;
};

/// The files of a PS2 disc, as io::Disc names them.
[[nodiscard]] std::vector<SourceFile> listFiles(const io::Disc& disc);

/// Finds which known disc `files` are: the one of `platform` whose every file is there with its size. `where` names
/// the disc in messages. Fails with ErrorCode::Invalid and a message a player can act on: not a disc of the game (no
/// key file and no archive), another release (the key file differs or is missing), or an incomplete or damaged copy
/// (a file missing or of another size).
[[nodiscard]] std::expected<const KnownDisc*, Error> identifyDisc(std::span<const SourceFile> files, Platform platform,
                                                                  std::span<const KnownDisc> known,
                                                                  std::string_view where);

/// What an install is given.
struct InstallRequest {
    const io::Disc* ps2 = nullptr; ///< Required: the player's PS2 disc, a folder or an ISO image.
    /// Files copied into the install folder's root as they are (Coney's executable); none when empty.
    std::vector<std::filesystem::path> programFiles;
    /// Re-read every file already installed and copy again any that does not match; otherwise a file of the right
    /// size is taken as finished, since only a file whose SHA-1 was checked is ever given its final name.
    bool verifyInstalled = false;
    std::span<const KnownDisc> known = knownDiscs(); ///< Tests pass their own.
    std::string version{coneyVersion()};             ///< Recorded in the manifest.
};

/// The steps an install reports, in order.
enum class InstallStep : std::uint8_t {
    Checking,  ///< Identifying the disc and the space needed.
    Verifying, ///< Re-reading a file already installed (verifyInstalled).
    Copying,   ///< Copying a file and its SHA-1.
    Program,   ///< Copying Coney's own files.
    Index,     ///< Writing the folders and index/install.json.
};

/// Where an install is; given to the progress function at least once per file and every few megabytes.
struct InstallProgress {
    InstallStep step = InstallStep::Checking;
    std::string_view file;      ///< The file's path in the install folder; empty outside a file.
    std::size_t fileNumber = 0; ///< From 1.
    std::size_t fileCount = 0;  ///< Files of the disc.
    std::uint64_t fileDone = 0; ///< Bytes of this file done.
    std::uint64_t fileSize = 0; ///< Bytes of this file.
    std::uint64_t done = 0;     ///< Bytes of all the disc's files done or found already installed.
    std::uint64_t total = 0;    ///< Bytes of all the disc's files.
};

/// Called with the progress; returning false cancels the install, which then stops as soon as it can.
using ProgressFunction = std::function<bool(const InstallProgress&)>;

/// What an install did.
struct InstallReport {
    bool cancelled = false; ///< Stopped by the progress function; run again to continue.
    std::size_t copied = 0; ///< Disc files copied in this run.
    std::size_t kept = 0;   ///< Disc files already installed and kept.
    std::uint64_t bytesCopied = 0;
    std::string ps2Disc;   ///< The known disc's id.
    std::string ps2Digest; ///< The disc's digest (install_manifest.h, listDigest()); empty when cancelled.
    std::string manifest;  ///< The text written to index/install.json (empty when cancelled).
};

/// Installs the PS2 disc into `folder`: copies each known file as it is into `game/ps2/`, checking its SHA-1 while it
/// copies, then Coney's own files into the root, makes the empty override folders and writes index/install.json. A
/// file is written under its name plus kPartSuffix and renamed once its SHA-1 matches, so a run stopped at any point
/// (cancelled, a crash, a full disk) leaves only finished files, and the next run continues from them.
/// index/install.json is removed first and written last, so it exists only for a finished install.
///
/// Fails with ErrorCode::Invalid for a disc that is not a known one (identifyDisc) or a file whose SHA-1 does not
/// match, ErrorCode::Io when the folder has too little space or cannot be written, and as the disc's reads fail.
///
/// docs/guides/running.md#install
[[nodiscard]] std::expected<InstallReport, Error> install(InstallFolder& folder, const InstallRequest& request,
                                                          const ProgressFunction& progress);

} // namespace coney::install
