// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace coney::raycast {

/// The name the game gives material `id`.
[[nodiscard]] std::string_view materialName(int id);

} // namespace coney::raycast
