// SPDX-License-Identifier: GPL-3.0-or-later
// Merging several script patches of one base (docs/guides/script-patches.md#merge): Coney's own, with no counterpart
// in the original.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "core/script_patch.h"

namespace coney::patch {

/// Which of two conflicting changes a merge keeps.
enum class MergeSide : std::uint8_t {
    Earlier, ///< The change of the patch earlier in load order.
    Later,   ///< The change of the later patch.
};

/// Two changes to the same place of the base: the same base line, or two different insertions at the same place.
struct MergeConflict {
    std::size_t earlier = 0; ///< The index, in the patches merged, of the earlier patch.
    std::size_t later = 0;   ///< The index of the later patch.
    /// The base lines each side's change covers, from `first` to before `end` (numbered from 1); `first == end` for an
    /// insertion before base line `first`.
    std::uint32_t earlierFirst = 1;
    std::uint32_t earlierEnd = 1;
    std::uint32_t laterFirst = 1;
    std::uint32_t laterEnd = 1;
    bool operator==(const MergeConflict&) const = default;
};

/// Decides a conflict: the player's recorded choice, or a fixed side.
using ConflictChooser = std::function<MergeSide(const MergeConflict&)>;

/// A merge's outcome: the merged section and every conflict met. With any conflict, `merged` holds at each one the
/// side the chooser picked, and the caller decides whether to use it (a player who has not chosen holds both mods).
struct MergeResult {
    PatchSection merged;
    std::vector<MergeConflict> conflicts;
};

/// Merges `sections`, all patches of the one text `base`, in load order (three or more merge one at a time): changes
/// to different base lines are applied together, ranges that touch but share no line merge, an identical change made
/// by both is applied once, and two changes to the same base line or two different insertions at the same place are a
/// conflict, settled by `choose` (unset: the earlier patch's change). An insertion at the edge of another patch's
/// changed range goes before it when it was placed before that range's first line, after it otherwise. Each section is
/// applied first, so any refusal of applyPatch() comes back as it is; sections with different `base`, `chunk` or
/// `form` are PatchFault::WrongBase. The merged section's `result` is the hash of the merged text.
[[nodiscard]] std::expected<MergeResult, PatchFailure>
mergePatches(std::span<const PatchSection> sections, std::string_view base, const ConflictChooser& choose = {});

} // namespace coney::patch
