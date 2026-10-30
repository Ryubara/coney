// SPDX-License-Identifier: GPL-3.0-or-later
// Applying and making script patches (docs/guides/script-patches.md): Coney's own, with no counterpart in the original.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/script_patch.h"
#include "core/sha256.h"

namespace coney::patch {

/// Applies `section` to `base` and returns the patched text. Every hash is checked: the base text's against the
/// section's `base` (PatchFault::WrongBase), each dropped range's (PatchFault::DamagedRange) and the result's
/// (PatchFault::DamagedResult). An operation that names a line or column `base` does not have, or a section that
/// leaves base lines unaccounted for, is PatchFault::DoesNotFit; a text past kMaxTextBytes or kMaxTextLines is
/// PatchFault::TooLarge. Line numbers are exact: a patch that does not fit is refused, never applied approximately.
[[nodiscard]] std::expected<std::string, PatchFailure> applyPatch(const PatchSection& section, std::string_view base);

/// The line a rebuild's `pieces` make of `baseLine`; nothing when a piece names a column the line does not have.
[[nodiscard]] std::optional<std::string> rebuildLine(std::span<const Piece> pieces, std::string_view baseLine);

/// What a made patch names besides its operations: the section's header, without the two text hashes, which
/// makePatch() computes.
struct PatchHeader {
    std::optional<std::uint32_t> entry{}; ///< The WAD entry the script lives in, when known.
    std::optional<std::string> name{};    ///< The script's name, when known.
    Sha256Digest chunk{};                 ///< The compiled chunk the base text was made from.
    std::string form = "lua4 1";          ///< The base text's form.
};

/// Makes the smallest patch from `base` to `edited` that holds no line of the base: a line diff, in which an added line
/// equal to a base line becomes a copy (`<`) and a changed line that keeps most of its base line becomes a rebuild
/// (`~`), so moving a block or changing one number carries none of the base's text. Applying the result to `base`
/// gives `edited`. A text past kMaxTextBytes or kMaxTextLines is PatchFault::TooLarge.
[[nodiscard]] std::expected<PatchSection, PatchFailure> makePatch(std::string_view base, std::string_view edited,
                                                                  const PatchHeader& header);

/// What a section does, in numbers, for a command's summary line.
struct PatchCounts {
    std::size_t dropped = 0;  ///< Base lines dropped.
    std::size_t added = 0;    ///< Lines the author wrote.
    std::size_t copied = 0;   ///< Base lines copied.
    std::size_t rebuilt = 0;  ///< Base lines rebuilt.
    std::size_t borrowed = 0; ///< Lines built from a base line elsewhere.
};

/// Counts what `section`'s operations do (PatchCounts). The lines kept are not counted: a keep to the end has as many
/// as the base, which the section alone does not tell.
[[nodiscard]] PatchCounts countOperations(const PatchSection& section);

} // namespace coney::patch
