// SPDX-License-Identifier: GPL-3.0-or-later
#include "studio/inspector.h"

#include <format>
#include <string>
#include <utility>
#include <vector>

namespace coney::studio {

namespace {

// One line of a group: its label and its value as text.
using Line = std::pair<std::string, std::string>;

// A point as the Inspector writes it, metres.
std::string metres(anim::Vec3 v) { return std::format("{:.2f}, {:.2f}, {:.2f} m", v.x, v.y, v.z); }

// An angle, degrees.
std::string degrees(float angle) { return std::format("{:.1f} deg", angle); }

// A colour's three channels.
std::string colour(const sandbox::Colour& c) { return std::format("{:.2f}, {:.2f}, {:.2f}", c.r, c.g, c.b); }

// Adds the group `title` to `page`: a submenu of watch items, one per line, as the entity panel's groups are.
void addGroup(debug::MenuPage& page, const std::string& title, std::vector<Line> lines) {
    page.add(debug::submenuItem(title, [title, lines = std::move(lines)] {
        auto group = std::make_shared<debug::MenuPage>(title);
        for (const auto& [label, value] : lines) {
            group->add(debug::watchItem(label, [value] { return value; }));
        }
        return group;
    }));
}

// The groups of a primitive: what it is, where, its shape's sizes, its look and its collision
// (docs/guides/sandbox.md#shapes).
void primitiveGroups(debug::MenuPage& page, const LevelDocument& level, const Entity& entity,
                     const sandbox::Primitive& p) {
    addGroup(page, "Identity",
             {{"Name", entity.name},
              {"Kind", std::format("Primitive ({})", sandbox::shapeName(p.shape))},
              {"Line", std::to_string(p.line)}});
    addGroup(page, "Transform", {{"Position", metres(p.base)}, {"Yaw", degrees(p.yawDegrees)}});
    std::vector<Line> shape;
    switch (p.shape) {
    case sandbox::Shape::Box:
    case sandbox::Shape::Ramp:
        shape.emplace_back("Size", std::format("{:.2f} x {:.2f} x {:.2f} m", p.size.x, p.size.y, p.size.z));
        break;
    case sandbox::Shape::Stairs:
        shape.emplace_back("Size", std::format("{:.2f} x {:.2f} x {:.2f} m", p.size.x, p.size.y, p.size.z));
        shape.emplace_back("Steps", std::to_string(p.steps));
        break;
    case sandbox::Shape::Cylinder:
    case sandbox::Shape::Capsule:
        shape.emplace_back("Radius", std::format("{:.2f} m", p.radius));
        shape.emplace_back("Height", std::format("{:.2f} m", p.size.z));
        shape.emplace_back("Segments", std::to_string(p.segments));
        break;
    case sandbox::Shape::Sphere:
        shape.emplace_back("Radius", std::format("{:.2f} m", p.radius));
        shape.emplace_back("Segments", std::to_string(p.segments));
        break;
    }
    addGroup(page, "Shape", std::move(shape));
    std::string texture = "none";
    if (p.texture && *p.texture < level.layout().textures.size()) {
        const auto& ref = level.layout().textures[*p.texture];
        texture = std::format("{} ({})", ref.name, ref.file);
    }
    addGroup(page, "Look",
             {{"Texture", texture}, {"Tiles per metre", std::format("{:.2f}", p.uvScale)}, {"Tint", colour(p.tint)}});
    addGroup(page, "Collision",
             {{"Solid", p.surface.solid ? "yes" : "no"},
              {"Material", std::to_string(p.surface.material)},
              {"Flags", std::format("0x{:04x}", p.surface.flags)},
              {"Area", std::to_string(p.surface.area)}});
}

// The groups of a spawn point, target, fighter or camera view.
void pointGroups(debug::MenuPage& page, const LevelDocument& level, const Entity& entity) {
    addGroup(page, "Identity", {{"Name", entity.name}, {"Kind", std::string(entityKindName(entity.kind))}});
    const auto& l = level.layout();
    switch (entity.kind) {
    case EntityKind::Spawn:
        addGroup(page, "Transform",
                 {{"Position", metres(entity.position)}, {"Heading", degrees(l.spawns[entity.index].headingDegrees)}});
        break;
    case EntityKind::Target:
        addGroup(page, "Transform",
                 {{"Position", metres(entity.position)}, {"Heading", degrees(l.targets[entity.index].headingDegrees)}});
        addGroup(page, "Combat", {{"Health", std::to_string(l.targets[entity.index].health)}});
        break;
    case EntityKind::Fighter:
        addGroup(
            page, "Transform",
            {{"Position", metres(entity.position)}, {"Heading", degrees(l.fighters[entity.index].headingDegrees)}});
        break;
    case EntityKind::View:
        addGroup(page, "Transform",
                 {{"Position", metres(entity.position)},
                  {"Yaw", degrees(l.views[entity.index].yawDegrees)},
                  {"Pitch", degrees(l.views[entity.index].pitchDegrees)}});
        break;
    case EntityKind::Primitive:
        break;
    }
}

// The level's own groups: what it is, and its sky, sun, ambient light, fog, occlusion and shadows.
void levelGroups(debug::MenuPage& page, const LevelDocument& level) {
    addGroup(page, "Identity",
             {{"Title", level.title()},
              {"File", level.path().filename().string()},
              {"Entities", std::to_string(level.entities().size())}});
    const sandbox::Lighting& light = level.layout().lighting;
    addGroup(page, "Lighting",
             {{"Sky", colour(light.sky)},
              {"Sun direction",
               std::format("{:.2f}, {:.2f}, {:.2f}", light.sunDirection.x, light.sunDirection.y, light.sunDirection.z)},
              {"Sun", colour(light.sun)},
              {"Ambient from the sky", colour(light.skyAmbient)},
              {"Ambient from the ground", colour(light.groundAmbient)},
              {"Fog", std::format("{:.1f} to {:.1f} m", light.fogStart, light.fogEnd)},
              {"Occlusion", light.occlusion
                                ? std::format("on, {:.1f} m, {:.2f}", light.occlusionRadius, light.occlusionStrength)
                                : std::string("off")},
              {"Shadows", light.shadows ? "on" : "off"}});
}

} // namespace

std::shared_ptr<debug::MenuPage> inspectorPage(const LevelDocument& level, std::optional<EntityId> entity) {
    auto page = std::make_shared<debug::MenuPage>(std::string(kInspectorTitle));
    const Entity* chosen = entity ? level.entity(*entity) : nullptr;
    if (chosen == nullptr) {
        levelGroups(*page, level);
    } else if (const sandbox::Primitive* primitive = level.primitive(*chosen)) {
        primitiveGroups(*page, level, *chosen, *primitive);
    } else {
        pointGroups(*page, level, *chosen);
    }
    return page;
}

} // namespace coney::studio
