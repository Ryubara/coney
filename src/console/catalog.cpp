// SPDX-License-Identifier: GPL-3.0-or-later
// A stub: the signatures with default results, so the tests compile and fail (the fails-first test commit).
#include "console/catalog.h"

namespace coney::console {

const CommandSpec* findSpec(std::span<const CommandSpec>, std::string_view) { return nullptr; }

std::expected<std::size_t, Error> registerBuiltins(Registry&, std::span<const CommandSpec>,
                                                   std::span<const BuiltinHandler>) {
    return fail(ErrorCode::InvalidArgument, "not implemented");
}

} // namespace coney::console
