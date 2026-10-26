// SPDX-License-Identifier: GPL-3.0-or-later
// The Debug draw page's mesh views (docs/guides/debug-menu.md#mesh-views): the switches' rules, colours and lines.
// Coney's own debug tooling.
#include "debug/mesh_views.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace coney::debug {

namespace {

// The flat views' palette: twelve opaque colours far enough apart to tell neighbours by (Coney's own choice).
constexpr std::array<graphics::Rgba, 12> kPalette{
    graphics::Rgba{230, 25, 75, 255},   graphics::Rgba{60, 180, 75, 255},  graphics::Rgba{255, 225, 25, 255},
    graphics::Rgba{0, 130, 200, 255},   graphics::Rgba{245, 130, 48, 255}, graphics::Rgba{145, 30, 180, 255},
    graphics::Rgba{70, 240, 240, 255},  graphics::Rgba{240, 50, 230, 255}, graphics::Rgba{210, 245, 60, 255},
    graphics::Rgba{250, 190, 212, 255}, graphics::Rgba{0, 128, 128, 255},  graphics::Rgba{170, 110, 40, 255}};

// The wireframe's colours (Coney's own choice): the selection, humans, objects, scenery.
constexpr graphics::Rgba kSelectionWire{255, 255, 255, 255};
constexpr graphics::Rgba kHumanWire{60, 255, 120, 255};
constexpr graphics::Rgba kObjectWire{255, 170, 40, 255};
constexpr graphics::Rgba kSceneryWire{170, 170, 170, 255};

// The Lod view's colours: full detail, fading out.
constexpr graphics::Rgba kLodFull{60, 200, 90, 255};
constexpr graphics::Rgba kLodFading{240, 220, 40, 255};

// A scope's name in `--mesh-view`.
std::expected<MeshScope, Error> scopeNamed(std::string_view name, std::string_view item) {
    if (name == "selection") {
        return MeshScope::Selection;
    }
    if (name == "humans") {
        return MeshScope::Humans;
    }
    if (name == "all") {
        return MeshScope::Everything;
    }
    return std::unexpected(
        Error{ErrorCode::InvalidArgument, std::format("--mesh-view: {} must be selection, humans or all", item)});
}

// A view's name in `--mesh-view`.
std::expected<MeshShading, Error> viewNamed(std::string_view name) {
    if (name == "wire") {
        return MeshShading::Wireframe;
    }
    if (name == "material") {
        return MeshShading::Material;
    }
    if (name == "draw") {
        return MeshShading::DrawCall;
    }
    if (name == "lod") {
        return MeshShading::Lod;
    }
    return std::unexpected(Error{ErrorCode::InvalidArgument, "--mesh-view: view must be wire, material, draw or lod"});
}

// A count as the Scene line prints it: "800", "12.3k", "1.20M".
std::string countText(double value) {
    if (value >= 1e6) {
        return std::format("{:.2f}M", value / 1e6);
    }
    if (value >= 1e3) {
        return std::format("{:.1f}k", value / 1e3);
    }
    return std::format("{:.0f}", value);
}

} // namespace

bool meshPassesOn(const MeshViewOptions& options) {
    return options.wireframe != MeshScope::Off ||
           (options.view != MeshShading::Shaded && options.viewScope != MeshScope::Off);
}

bool meshViewsOn(const MeshViewOptions& options) {
    return meshPassesOn(options) || options.normals || options.boneAxes;
}

bool inMeshScope(MeshScope scope, MeshKind kind, bool selected) {
    switch (scope) {
    case MeshScope::Off:
        return false;
    case MeshScope::Selection:
        return selected;
    case MeshScope::Humans:
        return selected || kind == MeshKind::Human;
    case MeshScope::Everything:
        return true;
    }
    return false;
}

MeshDrawPlan meshPlanFor(const MeshViewOptions& options, MeshKind kind, bool selected) {
    MeshDrawPlan plan;
    const bool viewed = options.view != MeshShading::Shaded && inMeshScope(options.viewScope, kind, selected);
    if (viewed) {
        plan.shaded = false;
        plan.flat = options.view != MeshShading::Wireframe;
        plan.colouring = plan.flat ? options.view : MeshShading::Shaded;
    }
    plan.wire = inMeshScope(options.wireframe, kind, selected) || (viewed && options.view == MeshShading::Wireframe);
    if (plan.wire) {
        plan.wireColour = meshWireColour(kind, selected);
    }
    return plan;
}

graphics::Rgba meshWireColour(MeshKind kind, bool selected) {
    if (selected) {
        return kSelectionWire;
    }
    switch (kind) {
    case MeshKind::Human:
        return kHumanWire;
    case MeshKind::Object:
        return kObjectWire;
    case MeshKind::Scenery:
        return kSceneryWire;
    }
    return kSceneryWire;
}

graphics::Rgba meshPaletteColour(std::uint32_t key) { return kPalette.at(key % kPalette.size()); }

std::uint32_t meshMaterialKey(std::string_view texture, std::size_t index) {
    // FNV-1a over the name, then the index mixed in, so neighbouring materials land on different colours.
    std::uint32_t hash = 2166136261U;
    for (const char c : texture) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619U;
    }
    return hash + static_cast<std::uint32_t>(index) * 7U;
}

graphics::Rgba meshLodColour(float fade) { return fade >= 1.0F ? kLodFull : kLodFading; }

anim::Mat34 characterPlacement(anim::Vec3 feet, float heading, float lean) {
    // As PlayLevelMode::skin() places a skinned vertex p: rolled by -lean about +y (x' = x cos + z sin, z' = -x sin + z
    // cos with the angle -lean), turned by the heading about z, moved to the feet.
    const float lc = std::cos(-lean);
    const float ls = std::sin(-lean);
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    // The roll's columns: where x, y and z go.
    const anim::Vec3 rollX{lc, 0.0F, -ls};
    const anim::Vec3 rollY{0.0F, 1.0F, 0.0F};
    const anim::Vec3 rollZ{ls, 0.0F, lc};
    const auto turn = [c, s](anim::Vec3 v) { return anim::Vec3{v.x * c - v.y * s, v.x * s + v.y * c, v.z}; };
    return anim::Mat34{turn(rollX), turn(rollY), turn(rollZ), feet};
}

std::size_t addNormals(DebugLines& lines, std::span<const anim::Vec3> positions, std::span<const anim::Vec3> normals,
                       const anim::Mat34& placement, float length) {
    const std::size_t count = std::min({positions.size(), normals.size(), kMaxNormals});
    for (std::size_t i = 0; i < count; ++i) {
        const anim::Vec3 from = anim::transformPoint(placement, positions[i]);
        const anim::Vec3 along = anim::scale(anim::normalise(anim::transformDirection(placement, normals[i])), length);
        lines.add(from, anim::add(from, along), kNormalColour);
    }
    return count;
}

std::size_t addBoneAxes(DebugLines& lines, std::span<const anim::Mat34> bones, std::span<const int> parents,
                        const anim::Mat34& placement, float length) {
    std::size_t drawn = 0;
    for (std::size_t i = 1; i < bones.size(); ++i) {
        const anim::Mat34 placed = anim::multiply(placement, bones[i]);
        const std::array<anim::Vec3, 3> axes{placed.x, placed.y, placed.z};
        for (std::size_t axis = 0; axis < axes.size(); ++axis) {
            lines.add(placed.t, anim::add(placed.t, anim::scale(anim::normalise(axes[axis]), length)),
                      kBoneAxisColours.at(axis));
        }
        // The link to its parent, but not to the root's motion (entry 0), which is no bone.
        if (i < parents.size() && parents[i] > 0 && static_cast<std::size_t>(parents[i]) < bones.size()) {
            const anim::Vec3 parent = anim::transformPoint(placement, bones[static_cast<std::size_t>(parents[i])].t);
            lines.add(placed.t, parent, kBoneAxisColours[1]);
        }
        ++drawn;
    }
    return drawn;
}

std::expected<MeshViewSpec, Error> parseMeshViewSpec(std::string_view text) {
    if (text.empty()) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "--mesh-view needs at least one item"});
    }
    MeshViewSpec spec;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(',', start), text.size());
        const std::string_view item = text.substr(start, end - start);
        const std::size_t equals = item.find('=');
        const std::string_view key = item.substr(0, equals);
        const std::string_view value = equals == std::string_view::npos ? std::string_view{} : item.substr(equals + 1);
        if (key == "wire" || key == "scope") {
            auto scope = scopeNamed(value, key);
            if (!scope) {
                return std::unexpected(std::move(scope.error()));
            }
            (key == "wire" ? spec.options.wireframe : spec.options.viewScope) = *scope;
        } else if (key == "view") {
            auto view = viewNamed(value);
            if (!view) {
                return std::unexpected(std::move(view.error()));
            }
            spec.options.view = *view;
        } else if (item == "normals") {
            spec.options.normals = true;
        } else if (item == "bones") {
            spec.options.boneAxes = true;
        } else if (item == "select=nearest") {
            spec.select = MeshViewSelect::Nearest;
        } else if (item == "select=player") {
            spec.select = MeshViewSelect::Player;
        } else if (key == "frames") {
            int frames = 0;
            const auto [rest, error] = std::from_chars(value.data(), value.data() + value.size(), frames);
            if (error != std::errc{} || rest != value.data() + value.size() || frames < 1) {
                return std::unexpected(
                    Error{ErrorCode::InvalidArgument, "--mesh-view: frames must be a whole number from 1"});
            }
            spec.frames = frames;
        } else {
            return std::unexpected(
                Error{ErrorCode::InvalidArgument, std::format("--mesh-view: unknown item '{}'", item)});
        }
        start = end + 1;
    }
    return spec;
}

std::string sceneCounterLine(std::span<const NamedCount> counters) {
    // The value of counter `name`, when there is one.
    const auto find = [counters](const std::string& name) -> std::optional<double> {
        for (const NamedCount& counter : counters) {
            if (counter.name == name) {
                return counter.value;
            }
        }
        return std::nullopt;
    };
    std::string line;
    for (const std::string_view kind : kSceneCounterKinds) {
        const std::string base = std::format("scene.{}.", kind);
        const std::optional<double> triangles = find(base + "triangles");
        const std::optional<double> vertices = find(base + "vertices");
        const std::optional<double> draws = find(base + "draws");
        if (!triangles && !vertices && !draws) {
            continue;
        }
        line += std::format("{}{} {} tris {} verts {} draws", line.empty() ? "" : "; ", kind,
                            countText(triangles.value_or(0.0)), countText(vertices.value_or(0.0)),
                            countText(draws.value_or(0.0)));
    }
    return line.empty() ? std::string("-") : line;
}

} // namespace coney::debug
