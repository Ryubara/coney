// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs: the reference lights nothing yet.
#include "graphics/vertex_light.h"

namespace coney::graphics {

Vu1Light vu1Light(const LightRecord& /*record*/) { return {}; }

std::vector<Vu1Light> vu1Lights(const LightManager& /*manager*/, const LightSelection& /*selection*/) { return {}; }

LightColour lightAtVertex(world::Vec3 /*position*/, world::Vec3 /*normal*/, std::span<const Vu1Light> /*lights*/) {
    return {};
}

LightColour referenceColour(const ReferenceVertex& /*vertex*/, std::span<const Vu1Light> /*lights*/,
                            LightColour /*material*/) {
    return {};
}

std::vector<GsColour> lightVertices(std::span<const ReferenceVertex> /*vertices*/, std::span<const Vu1Light> /*lights*/,
                                    LightColour /*material*/) {
    return {};
}

GsColour toGs(const LightColour& /*colour*/) { return {}; }

LightColour prelightFromPacked(const std::array<std::uint8_t, 4>& /*rgba*/) { return {}; }

LightColour materialFromRgba(const std::array<std::uint8_t, 4>& /*rgba*/) { return {}; }

} // namespace coney::graphics
