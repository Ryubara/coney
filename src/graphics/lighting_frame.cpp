// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/lighting_frame.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace coney::graphics {

namespace {

// A copy of one of the manager's lists.
// @coney-only the render-side lighting frame
std::vector<std::uint16_t> copyOf(std::span<const std::uint16_t> list) { return {list.begin(), list.end()}; }

// The frame's view of record `index`.
// @coney-only the render-side lighting frame
FrameLight frameLightOf(const LightRecord& record, std::uint16_t index) {
    return FrameLight{.record = index,
                      .type = record.desc.type,
                      .lights = record.desc.lights,
                      .colour = record.current,
                      .base = record.desc.colour,
                      .position = record.desc.position,
                      .direction = record.desc.direction,
                      .radius = record.desc.radius,
                      .coneAngle = record.desc.coneAngle,
                      .flicker = record.desc.effects & kEffectFlickerMask,
                      .corona = record.desc.corona};
}

} // namespace

// A binary search of the lights, which are in record order.
// @coney-only the render-side lighting frame
const FrameLight* LightingFrame::light(std::uint16_t record) const {
    const auto found = std::ranges::lower_bound(lights, record, {}, &FrameLight::record);
    return found != lights.end() && found->record == record ? &*found : nullptr;
}

LightingFrame buildLightingFrame(const LevelLighting& level, const LightView& view) {
    const LightManager& manager = level.lights;
    LightingFrame frame;
    frame.view = view;
    frame.worldList = copyOf(manager.worldList());
    frame.objectList = copyOf(manager.objectList());
    frame.pulseList = copyOf(manager.pulseList());
    frame.points = copyOf(manager.visiblePoints());
    frame.worldPoints = copyOf(manager.visibleWorldPoints());

    // Every record of a list once, in record order, so light() can search them.
    std::vector<std::uint16_t> records;
    for (const std::vector<std::uint16_t>* list :
         {&frame.worldList, &frame.objectList, &frame.pulseList, &frame.points}) {
        records.insert(records.end(), list->begin(), list->end());
    }
    std::ranges::sort(records);
    const auto [first, last] = std::ranges::unique(records);
    records.erase(first, last);
    frame.lights.reserve(records.size());
    for (const std::uint16_t index : records) {
        frame.lights.push_back(frameLightOf(manager.record(index), index));
    }

    frame.worldAmbient = manager.record(LightManager::kWorldAmbient).current;
    frame.fogColour = level.fog.colour;
    frame.fogStart = level.fog.start;
    frame.shadow = frameShadowLight(level.shadow);
    const std::span<const std::uint32_t> off = level.casters.disabled();
    frame.shadowsOff.assign(off.begin(), off.end());
    return frame;
}

std::optional<FrameShadowLight> frameShadowLight(const ShadowLight& shadow) {
    if (!shadow.scripted) {
        return std::nullopt;
    }
    const world::Vec3 offset = gameToRenderWare(shadow.offset.x, shadow.offset.y, shadow.offset.z);
    const float length = std::sqrt((offset.x * offset.x) + (offset.y * offset.y) + (offset.z * offset.z));
    // An offset of 0 has no direction: the light then shines straight down.
    const world::Vec3 direction = length > 0.0F
                                      ? world::Vec3{-offset.x / length, -offset.y / length, -offset.z / length}
                                      : world::Vec3{0.0F, -1.0F, 0.0F};
    return FrameShadowLight{.colour = shadow.colour, .offset = offset, .direction = direction};
}

} // namespace coney::graphics
