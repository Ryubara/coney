// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/texture_data.h"

namespace coney::graphics {

// Stubs so the tests compile and fail first; the next commit implements them.

std::size_t bytesPerTexel(TextureFormat) { return 0; }

std::expected<void, Error> validateTexture(const TextureData&) { return {}; }

std::uint64_t hashTexture(const TextureData&) { return 0; }

} // namespace coney::graphics
