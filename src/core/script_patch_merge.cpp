// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/script_patch_merge.h"

namespace coney::patch {

std::expected<MergeResult, PatchFailure> mergePatches(std::span<const PatchSection> /*sections*/,
                                                      std::string_view /*base*/, const ConflictChooser& /*choose*/) {
    return std::unexpected(PatchFailure{});
}

} // namespace coney::patch
