// SPDX-License-Identifier: GPL-3.0-or-later
// A stub: the signatures with default results, so the tests compile and fail (the fails-first test commit).
#include "console/tokenizer.h"

namespace coney::console {

std::expected<std::vector<std::string>, Error> tokenize(std::string_view) {
    return fail(ErrorCode::InvalidArgument, "not implemented");
}

SplitLine splitForCompletion(std::string_view) { return {}; }

std::string quoteWord(std::string_view) { return {}; }

} // namespace coney::console
