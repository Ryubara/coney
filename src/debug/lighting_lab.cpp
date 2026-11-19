// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/lighting_lab.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <system_error>

namespace coney::debug {

namespace {

// RenderWare's axes (y up) into the game's (z up): (x, y, z) is (x, -z, y), the reverse of gameToRenderWare().
anim::Vec3 gameOf(world::Vec3 p) { return anim::Vec3{p.x, -p.z, p.y}; }

// A light's colour as a line's: each component × 255, clamped, opaque.
graphics::Rgba lineColourOf(const graphics::LightColour& colour) {
    const auto byte = [](float v) { return static_cast<std::uint8_t>(std::clamp(v * 255.0F, 0.0F, 255.0F)); };
    return graphics::Rgba{byte(colour.r), byte(colour.g), byte(colour.b), 255};
}

// A number as the dump writes it: the shortest text that reads back as the same float.
std::string number(float value) { return std::format("{}", value); }

// A colour as the lab's lines show it: three components to two places.
std::string colourText(const graphics::LightColour& colour) {
    return std::format("({:.2f}, {:.2f}, {:.2f})", colour.r, colour.g, colour.b);
}

// The flicker mode's name.
std::string_view flickerName(std::uint32_t mode) {
    switch (mode) {
    case 0:
        return "none";
    case graphics::kFlickerRandom:
        return "random";
    case graphics::kFlickerFade:
        return "fade";
    case graphics::kFlickerBurst:
        return "burst";
    case graphics::kFlickerBlink:
        return "blink";
    default:
        return "other";
    }
}

// The squared distance from `a` to `b`.
float distanceSq(world::Vec3 a, world::Vec3 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return (dx * dx) + (dy * dy) + (dz * dz);
}

// The selection's overlap score of a light at `position` of `radius` with `sphere`: the squared distance less the
// squared sum of the radii (docs/research/lighting.md#select); 0 or below overlaps.
float scoreOf(const graphics::LightSphere& sphere, world::Vec3 position, float radius) {
    const float reach = sphere.radius + radius;
    return distanceSq(position, sphere.centre) - (reach * reach);
}

// Writes `text` to `path`, replacing the file.
std::expected<void, Error> writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        return fail(ErrorCode::Io, std::format("{}: cannot write the lighting dump", path.string()));
    }
    return {};
}

} // namespace

LightingCounts lightingCounts(const graphics::LightingFrame& frame, const graphics::LightManager& manager) {
    LightingCounts counts;
    counts.records = manager.count();
    counts.points = frame.points.size();
    counts.worldPoints = frame.worldPoints.size();
    counts.coronas = frame.coronas;
    counts.atomics = frame.selections.size();
    for (const graphics::FrameSelection& selection : frame.selections) {
        if (selection.full()) {
            ++counts.full;
        }
        if (selection.caller != graphics::SelectionCaller::Human) {
            continue;
        }
        if (selection.count >= 3 && selection.count <= 6) {
            ++counts.humansByLights.at(selection.count - 3);
        } else {
            ++counts.humansOther;
        }
    }
    return counts;
}

std::string_view lightKindName(graphics::LightType type, float radius) {
    switch (type) {
    case graphics::LightType::Point:
        return radius > 0.0F ? "point" : "corona only";
    case graphics::LightType::Spot:
        return "spot";
    case graphics::LightType::Directional:
        return "directional";
    case graphics::LightType::Ambient:
        return "ambient";
    }
    return "light";
}

std::string_view lightsWhatName(std::uint16_t lights) {
    const bool objects = (lights & graphics::kLightsObjects) != 0;
    const bool world = (lights & graphics::kLightsWorld) != 0;
    if (objects && world) {
        return "both";
    }
    if (objects) {
        return "objects";
    }
    return world ? "world" : "nothing";
}

std::string lightOriginName(graphics::LightOrigin origin, std::uint16_t record) {
    switch (origin) {
    case graphics::LightOrigin::BuiltIn:
        if (record == graphics::LightManager::kWorldAmbient) {
            return "built in (A)";
        }
        if (record == graphics::LightManager::kPulse) {
            return "built in (B)";
        }
        return record == graphics::LightManager::kGlow ? "built in (glow)" : "built in";
    case graphics::LightOrigin::StandIn:
        return "Coney's stand-in";
    case graphics::LightOrigin::Untagged:
        break;
    }
    return "script or light task";
}

std::string_view selectionCallerName(graphics::SelectionCaller caller) {
    switch (caller) {
    case graphics::SelectionCaller::Sector:
        return "sector";
    case graphics::SelectionCaller::Background:
        return "background";
    case graphics::SelectionCaller::Human:
        return "human";
    case graphics::SelectionCaller::Object:
        return "object";
    }
    return "atomic";
}

std::uint32_t selectionCallerAddress(graphics::SelectionCaller caller) {
    switch (caller) {
    case graphics::SelectionCaller::Sector:
        return 0x00411aa8;
    case graphics::SelectionCaller::Background:
        return 0x0040d1a8;
    case graphics::SelectionCaller::Human:
        return 0x001746f8;
    case graphics::SelectionCaller::Object:
        return 0x001803a4;
    }
    return 0;
}

std::uint32_t renderWareLightType(graphics::LightType type) {
    switch (type) {
    case graphics::LightType::Point:
        return 0x80;
    case graphics::LightType::Spot:
        return 0x81;
    case graphics::LightType::Directional:
        return 1;
    case graphics::LightType::Ambient:
        return 2;
    }
    return 0;
}

std::string lightLine(const graphics::LightManager& manager, std::uint16_t index) {
    const graphics::LightRecord& record = manager.record(index);
    const graphics::LightDescriptor& desc = record.desc;
    std::string line = std::format("#{} {}, lights {}, {}, colour {} of base {}", index,
                                   lightKindName(desc.type, desc.radius), lightsWhatName(desc.lights),
                                   desc.on ? "on" : "off", colourText(record.current), colourText(desc.colour));
    if (desc.type == graphics::LightType::Point || desc.type == graphics::LightType::Spot) {
        line += std::format(", radius {:.2f} m", desc.radius);
    }
    if (desc.corona >= 0) {
        line += std::format(", corona {} (height {:.2f}, pull {:.2f}, size {:.2f})", desc.corona, desc.coronaHeight,
                            desc.coronaPull, desc.coronaSize);
    }
    const std::uint32_t flicker = desc.effects & graphics::kEffectFlickerMask;
    if (flicker != 0) {
        line += std::format(", flicker {} ({} ms)", flickerName(flicker), record.timerMs);
    }
    line += ", " + lightOriginName(manager.origin(index), index);
    return line;
}

std::string selectionSummary(const graphics::FrameSelection& selection) {
    const anim::Vec3 at = gameOf(selection.sphere.centre);
    return std::format("{} at ({:.2f}, {:.2f}, {:.2f}), {} lights of a cap of {} at {:.1f} m",
                       selectionCallerName(selection.caller), at.x, at.y, at.z, selection.count, selection.cap,
                       std::sqrt(selection.distSq));
}

std::vector<std::string> selectionLines(const graphics::FrameSelection& selection,
                                        const graphics::LightingFrame& frame) {
    std::vector<std::string> lines;
    std::size_t place = 0;
    for (const graphics::ChosenLight& light : selection.chosen()) {
        ++place;
        const bool point = light.type == graphics::LightType::Point || light.type == graphics::LightType::Spot;
        std::string from = "the copied list";
        if (light.record == graphics::LightManager::kGlow) {
            from = "the glow";
        } else if (point) {
            from = "the point lists";
        }
        std::string line =
            std::format("{}. #{} {}, from {}", place, light.record, lightKindName(light.type, light.radius), from);
        if (point) {
            line += std::format(", score {:.2f}", scoreOf(selection.sphere, light.position, light.radius));
        }
        lines.push_back(std::move(line));
    }
    // The point lights of the list the call read that overlapped the sphere and were not kept.
    if (selection.pointLights) {
        for (const std::uint16_t record : selection.objects ? frame.points : frame.worldPoints) {
            const graphics::FrameLight* light = frame.light(record);
            const auto chosen = selection.chosen();
            if (light == nullptr ||
                std::ranges::any_of(chosen, [record](const graphics::ChosenLight& c) { return c.record == record; })) {
                continue;
            }
            const float score = scoreOf(selection.sphere, light->position, light->radius);
            if (score <= 0.0F) {
                lines.push_back(std::format("dropped: #{} {}, score {:.2f}", record,
                                            lightKindName(light->type, light->radius), score));
            }
        }
    }
    return lines;
}

const graphics::FrameSelection* selectionNear(const graphics::LightingFrame& frame, graphics::SelectionCaller caller,
                                              anim::Vec3 at) {
    const graphics::FrameSelection* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (const graphics::FrameSelection& selection : frame.selections) {
        if (selection.caller != caller) {
            continue;
        }
        const anim::Vec3 centre = gameOf(selection.sphere.centre);
        const float dx = centre.x - at.x;
        const float dy = centre.y - at.y;
        const float dz = centre.z - at.z;
        const float d = (dx * dx) + (dy * dy) + (dz * dz);
        if (d < best) {
            best = d;
            nearest = &selection;
        }
    }
    return nearest;
}

const graphics::FrameSelection* labSelection(const graphics::LightingFrame& frame, std::span<const EntityRef> entities,
                                             EntityId inspected, anim::Vec3 playerFeet) {
    if (const EntityRef* entity = findEntity(entities, inspected); entity != nullptr) {
        return selectionNear(frame,
                             entity->kind == EntityKind::Object ? graphics::SelectionCaller::Object
                                                                : graphics::SelectionCaller::Human,
                             entity->position);
    }
    return selectionNear(frame, graphics::SelectionCaller::Human, playerFeet);
}

DebugLines lightingLines(const graphics::LightingFrame& frame, const LightingDrawOptions& options,
                         const graphics::FrameSelection* selected) {
    DebugLines lines;
    for (const graphics::FrameLight& light : frame.lights) {
        const anim::Vec3 at = gameOf(light.position);
        const bool point = light.type == graphics::LightType::Point || light.type == graphics::LightType::Spot;
        if (options.pointLights && point && light.radius > 0.0F) {
            lines.sphere(at, light.radius, lineColourOf(light.colour));
        }
        if (options.coronaOnly && point && light.radius <= 0.0F) {
            lines.cross(at, 0.5F, lineColourOf(light.colour));
        }
        if (options.directional && light.type == graphics::LightType::Directional) {
            constexpr float kArrowLength = 2.0F;
            const anim::Vec3 along = gameOf(light.direction);
            lines.arrow(gameOf(frame.view.position), anim::scale(along, kArrowLength), lineColourOf(light.colour));
        }
        if (options.marked >= 0 && light.record == static_cast<std::uint16_t>(options.marked)) {
            constexpr graphics::Rgba kMarked{255, 255, 0, 255};
            lines.cross(at, 1.5F, kMarked);
        }
    }
    if (options.cullReach) {
        constexpr float kCullFraction = 0.75F; // the cull's reach, docs/research/lighting.md#cull
        lines.circle(gameOf(frame.view.position), frame.view.farClip * kCullFraction, anim::Vec3{0.0F, 0.0F, 1.0F},
                     graphics::Rgba{255, 160, 40, 255});
    }
    if (options.selection && selected != nullptr) {
        constexpr graphics::Rgba kSphere{80, 255, 255, 255};
        const anim::Vec3 centre = gameOf(selected->sphere.centre);
        lines.sphere(centre, selected->sphere.radius, kSphere);
        for (const graphics::ChosenLight& light : selected->chosen()) {
            if (light.type == graphics::LightType::Point || light.type == graphics::LightType::Spot) {
                lines.add(centre, gameOf(light.position), lineColourOf(light.colour));
            }
        }
    }
    return lines;
}

std::vector<std::string> selectionColumns() {
    std::vector<std::string> columns{"caller", "dist_sq",      "sphere_x", "sphere_y", "sphere_z", "sphere_radius",
                                     "flags",  "point_lights", "pulse",    "human",    "count"};
    for (std::size_t i = 0; i < graphics::kMaxSelectedLights; ++i) {
        for (const std::string_view field : {"type", "radius", "r", "g", "b", "a", "x", "y", "z"}) {
            columns.push_back(std::format("light{}_{}", i, field));
        }
    }
    return columns;
}

// Stub: none.
std::vector<TraceLight> traceLightsOf(const graphics::FrameSelection& /*selection*/) { return {}; }

// Stub: none.
std::vector<std::string> originalSelectionLines(std::span<const TraceCall> /*trace*/,
                                                const graphics::FrameSelection& /*selection*/) {
    return {};
}

std::string selectionsCsv(const graphics::LightingFrame& frame) {
    std::string text;
    const std::vector<std::string> columns = selectionColumns();
    for (std::size_t i = 0; i < columns.size(); ++i) {
        text += (i == 0 ? "" : ",") + columns[i];
    }
    text += '\n';
    for (const graphics::FrameSelection& s : frame.selections) {
        text += std::format("0x{:08x},{},{},{},{},{},{},{},{},{},{}", selectionCallerAddress(s.caller),
                            number(s.distSq), number(s.sphere.centre.x), number(s.sphere.centre.y),
                            number(s.sphere.centre.z), number(s.sphere.radius), s.objects ? 1 : 2,
                            s.pointLights ? 1 : 0, s.pulse ? 1 : 0, s.human ? 1 : 0, s.count);
        for (std::size_t i = 0; i < graphics::kMaxSelectedLights; ++i) {
            if (i >= s.count) {
                text += ",,,,,,,,,";
                continue;
            }
            const graphics::ChosenLight& l = s.lights.at(i);
            text += std::format(",{},{},{},{},{},{},{},{},{}", renderWareLightType(l.type), number(l.radius),
                                number(l.colour.r), number(l.colour.g), number(l.colour.b), number(l.colour.a),
                                number(l.position.x), number(l.position.y), number(l.position.z));
        }
        text += '\n';
    }
    return text;
}

std::string frameLightsCsv(const graphics::LightingFrame& frame) {
    std::string text = "record,type,lights,r,g,b,a,base_r,base_g,base_b,base_a,x,y,z,dir_x,dir_y,dir_z,radius,cone,"
                       "corona,flicker,lists\n";
    // Which of the cull's lists hold `record`: 1 list A, 2 list B, 4 list C, 8 the point lights, 16 the world's.
    const auto listsOf = [&frame](std::uint16_t record) {
        const auto in = [record](const std::vector<std::uint16_t>& list) {
            return std::ranges::find(list, record) != list.end();
        };
        return (in(frame.worldList) ? 1 : 0) | (in(frame.objectList) ? 2 : 0) | (in(frame.pulseList) ? 4 : 0) |
               (in(frame.points) ? 8 : 0) | (in(frame.worldPoints) ? 16 : 0);
    };
    for (const graphics::FrameLight& l : frame.lights) {
        text += std::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n", l.record,
                            renderWareLightType(l.type), l.lights, number(l.colour.r), number(l.colour.g),
                            number(l.colour.b), number(l.colour.a), number(l.base.r), number(l.base.g),
                            number(l.base.b), number(l.base.a), number(l.position.x), number(l.position.y),
                            number(l.position.z), number(l.direction.x), number(l.direction.y), number(l.direction.z),
                            number(l.radius), number(l.coneAngle), l.corona, l.flicker, listsOf(l.record));
    }
    return text;
}

std::expected<std::filesystem::path, Error> writeLightingDump(const graphics::LightingFrame& frame,
                                                              const std::filesystem::path& folder) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    // The first number whose files are not there yet, so a dump never replaces an earlier one.
    for (int n = 1; n < 100000; ++n) {
        std::filesystem::path selections = folder / std::format("lighting-{}-selections.csv", n);
        const std::filesystem::path lights = folder / std::format("lighting-{}-lights.csv", n);
        if (std::filesystem::exists(selections, ec) || std::filesystem::exists(lights, ec)) {
            continue;
        }
        if (auto written = writeText(lights, frameLightsCsv(frame)); !written) {
            return std::unexpected(std::move(written.error()));
        }
        if (auto written = writeText(selections, selectionsCsv(frame)); !written) {
            return std::unexpected(std::move(written.error()));
        }
        return selections;
    }
    return fail(ErrorCode::Io, std::format("{}: no free name for a lighting dump", folder.string()));
}

std::size_t LightEditQueue::applyPending(graphics::LightManager* manager) {
    std::size_t applied = 0;
    if (manager != nullptr) {
        for (const LightEdit& edit : m_pending) {
            const graphics::LightHandle handle = static_cast<graphics::LightHandle>(edit.record) + 1;
            const graphics::LightRecord* record = manager->light(handle);
            if (record == nullptr) {
                continue;
            }
            graphics::LightDescriptor desc = record->desc;
            if (edit.radius) {
                desc.radius = *edit.radius;
            }
            const bool recoloured = edit.colour[0] || edit.colour[1] || edit.colour[2];
            desc.colour.r = edit.colour[0].value_or(desc.colour.r);
            desc.colour.g = edit.colour[1].value_or(desc.colour.g);
            desc.colour.b = edit.colour[2].value_or(desc.colour.b);
            if (edit.radius || recoloured) {
                static_cast<void>(manager->setLight(handle, desc));
            }
            if (edit.on) {
                static_cast<void>(manager->setOn(handle, *edit.on));
            }
            ++applied;
        }
    }
    m_pending.clear();
    return applied;
}

} // namespace coney::debug
