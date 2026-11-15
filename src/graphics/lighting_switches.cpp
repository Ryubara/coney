// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/lighting_switches.h"

namespace coney::graphics {

LightSelection layeredSelection(const LightSelection& selection, const LightManager& /*manager*/,
                                const LightingLayers& /*layers*/, LightingView /*view*/) {
    return selection;
}

LightColour layeredColour(const LightManager& manager, std::uint16_t index, const LightingLayers& /*layers*/) {
    return manager.record(index).current;
}

LitMoment LightingHold::choose(const LitMoment& wanted, bool /*hold*/) { return wanted; }

} // namespace coney::graphics
