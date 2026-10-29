// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the tests-first commit: the tests compile against it and fail.
#include "studio/run_in_coney.h"

namespace coney::studio {

std::vector<std::string> runInConeyCommand(const RunRequest&) { return {}; }

std::filesystem::path coneyBeside(const std::filesystem::path&) { return {}; }

} // namespace coney::studio
