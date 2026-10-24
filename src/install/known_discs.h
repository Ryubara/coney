// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace coney::install {

/// Which console a disc is for; it decides the install folder it goes into (`game/ps2/`; `game/xbox/` is reserved
/// for the Xbox disc, which the install does not take yet).
enum class Platform : std::uint8_t { Ps2, Xbox };

/// One file of a known disc: its name as the disc spells it (`IOP/BFW.SND`, `bik/L1_IN_hd.bik`), its size and its
/// SHA-1 as 40 lower-case hex digits.
struct KnownFile {
    std::string_view name;
    std::uint64_t size;
    std::string_view sha1;
};

/// A release of the game the installer accepts, and the files it copies from it.
struct KnownDisc {
    std::string_view id;              ///< Recorded in index/install.json: `ps2-ntsc-u`.
    std::string_view title;           ///< For people: "The Warriors (PlayStation 2, NTSC-U, SLUS-21215)".
    Platform platform;                ///< The install folder it goes into.
    std::string_view keyFile;         ///< The file that names the release: the PS2 boot executable.
    std::span<const KnownFile> files; ///< Every file the install copies, in the order it copies them.
};

/// The releases Coney installs from, with every file's size and SHA-1: a curated reference list (LEGAL.md, "Reference
/// lists"; counts and SHA-1s only, never the files): today the NTSC-U PS2 disc, every file on it
/// (docs/research/discs.md).
[[nodiscard]] std::span<const KnownDisc> knownDiscs();

} // namespace coney::install
