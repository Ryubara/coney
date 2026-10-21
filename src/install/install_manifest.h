// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "install/known_discs.h"

namespace coney::install {

/// One installed file, as index/install.json lists it.
struct ManifestFile {
    Platform platform = Platform::Ps2;
    std::string name; ///< As the disc spells it (`IOP/BFW.SND`); the file is `<platform folder>/<name>`.
    std::uint64_t size = 0;
    std::string sha1; ///< 40 lower-case hex digits.
};

/// One disc an install was made from.
struct ManifestSource {
    const KnownDisc* disc = nullptr;
    std::string path; ///< The disc's folder or image as the player gave it.
    bool image = false;
};

/// A program file copied into the install's root (Coney's executable).
struct ProgramFile {
    std::string name;
    std::uint64_t size = 0;
    std::string sha1;
};

/// Everything index/install.json records.
struct Manifest {
    std::string version;
    std::vector<ManifestSource> sources;
    std::vector<ManifestFile> files; ///< In copy order.
    std::vector<ProgramFile> program;
};

/// The type a disc file is counted under in the manifest: `archive`, `sound`, `movies`, `executable`, `modules` or
/// `system` for the PS2 (`other` for anything else).
[[nodiscard]] std::string_view fileType(Platform platform, std::string_view name);

/// SHA-1 over one line `<name> <size> <sha1>\n` per file, in the order given: the digest of a disc (its identity) or
/// of one type of its files. Any changed, added or removed file changes it.
[[nodiscard]] std::string listDigest(std::span<const ManifestFile> files);

/// index/install.json's text: the format number, Coney's version, each source (its known disc, path and digest), per
/// platform and type the file count, bytes and digest, every file with its size and SHA-1, and the program files.
/// The same install always gives the same bytes: no times, the files in copy order, and two-space indents.
[[nodiscard]] std::string manifestJson(const Manifest& manifest);

} // namespace coney::install
