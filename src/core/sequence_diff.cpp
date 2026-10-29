// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/sequence_diff.h"

namespace coney::patch {

std::vector<DiffStep> diffSequences(std::span<const std::uint32_t> /*a*/, std::span<const std::uint32_t> /*b*/,
                                    std::size_t /*maxCost*/) {
    return {};
}

} // namespace coney::patch
