// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/body_size.h"

namespace coney::characters {

std::optional<float> randomBodySize(const BodySizeInput& /*input*/, GameRandom& /*random*/) {
    // Stub: no random size, no draw.
    return std::nullopt;
}

} // namespace coney::characters
