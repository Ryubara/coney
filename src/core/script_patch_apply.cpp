// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/script_patch_apply.h"

namespace coney::patch {

std::expected<std::string, PatchFailure> applyPatch(const PatchSection& /*section*/, std::string_view /*base*/) {
    return std::unexpected(PatchFailure{});
}

std::expected<PatchSection, PatchFailure> makePatch(std::string_view /*base*/, std::string_view /*edited*/,
                                                    const PatchHeader& /*header*/) {
    return std::unexpected(PatchFailure{});
}

PatchCounts countOperations(const PatchSection& /*section*/) { return {}; }

} // namespace coney::patch
