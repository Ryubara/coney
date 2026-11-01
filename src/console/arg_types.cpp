// SPDX-License-Identifier: GPL-3.0-or-later
// A stub: the signatures with default results, so the tests compile and fail (the fails-first test commit).
#include "console/arg_types.h"

namespace coney::console {

std::expected<void, std::string> checkArg(const ArgSpec&, std::string_view, std::span<const std::string>,
                                          const ValueSource&) {
    return std::unexpected(std::string("not implemented"));
}

std::vector<std::string> completeArg(const ArgSpec&, std::string_view, std::span<const std::string>,
                                     const ValueSource&) {
    return {};
}

} // namespace coney::console
