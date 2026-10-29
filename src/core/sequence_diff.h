// SPDX-License-Identifier: GPL-3.0-or-later
// A shortest edit script between two sequences, for script patches (docs/guides/script-patches.md#make): Coney's own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace coney::patch {

/// One step of an edit script from sequence `a` to sequence `b`.
enum class DiffStep : std::uint8_t {
    Same,   ///< The next element of `a` equals the next of `b`; both advance.
    Delete, ///< The next element of `a` is left out.
    Insert, ///< The next element of `b` is put in.
};

/// The most edits one part of a diff searches before it gives that part up as replaced whole. A script's edit is a few
/// hundred lines at most; the cap keeps a diff of two unrelated texts from taking minutes.
inline constexpr std::size_t kMaxDiffCost = 8192;

/// An edit script from `a` to `b` (Myers' O(ND) diff in linear space): the fewest Delete and Insert steps while the
/// cost stays within `maxCost` per part, a correct but longer one past it. The Same steps follow both sequences in
/// order, so replaying the script turns `a` into `b`.
[[nodiscard]] std::vector<DiffStep> diffSequences(std::span<const std::uint32_t> a, std::span<const std::uint32_t> b,
                                                  std::size_t maxCost = kMaxDiffCost);

} // namespace coney::patch
