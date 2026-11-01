// SPDX-License-Identifier: GPL-3.0-or-later
// A stub: the signatures with default results, so the tests compile and fail (the fails-first test commit).
#include "console/owned_spec.h"

namespace coney::console {

OwnedSpec::OwnedSpec(const CommandSpec& spec) : m_spec(spec) {}

std::string_view OwnedSpec::keep(std::string_view) { return {}; }

} // namespace coney::console
