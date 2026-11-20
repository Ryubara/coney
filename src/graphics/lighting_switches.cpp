// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/lighting_switches.h"

namespace coney::graphics {

namespace {

// Whether `type` is a point or spot light.
// @coney-only the lighting lab's layers
bool isPoint(LightType type) { return type == LightType::Point || type == LightType::Spot; }

} // namespace

LightSelection layeredSelection(const LightSelection& selection, const LightManager& manager,
                                const LightingLayers& layers, LightingView view) {
    LightSelection kept;
    if (view == LightingView::PrelightOnly) {
        return kept;
    }
    for (const std::uint16_t index : selection.lights()) {
        const LightType type = manager.record(index).desc.type;
        const bool keep = index == LightManager::kWorldAmbient ? layers.worldAmbient
                          : isPoint(type)                      ? layers.pointLights
                                                               : layers.objectLights;
        if (keep) {
            kept.records.at(kept.count++) = index;
        }
    }
    return kept;
}

LightColour layeredColour(const LightManager& manager, std::uint16_t index, const LightingLayers& layers) {
    const LightRecord& record = manager.record(index);
    if (isPoint(record.desc.type)) {
        // The glow's colour is the human's, set by the selection: it has no base of its own to fall back to.
        return layers.flicker || index == LightManager::kGlow ? record.current : record.desc.colour;
    }
    if (layers.brightness) {
        return record.current;
    }
    // Every ambient and directional light shines with its base plus the brightness and offset (the world ambient's
    // base is set at each viewport), so taking the added colour away gives the base.
    const LightColour added = manager.addedColour();
    return LightColour{record.current.r - added.r, record.current.g - added.g, record.current.b - added.b,
                       record.current.a};
}

// Stub: the level's fog, as before the change.
WorldFog shownFog(const WorldFog& level, const FogSwitches& /*switches*/) { return level; }

// The held moment while held and no step ran since it was chosen; else the one asked for, which is then held.
// @coney-only the lighting lab's Hold lighting
LitMoment LightingHold::choose(const LitMoment& wanted, bool hold) {
    if (hold && m_held && m_heldAt == m_steps) {
        return *m_held;
    }
    m_held = wanted;
    m_heldAt = m_steps;
    return wanted;
}

} // namespace coney::graphics
