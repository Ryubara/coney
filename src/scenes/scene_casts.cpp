// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the `--scene` preview's cast; the original only plays a scene with the humans and objects a script binds
#include "scenes/scene_casts.h"

#include <algorithm>
#include <array>
#include <utility>

namespace coney::scenes {

namespace {

// The table's row for `scene`, or null when no script builds a table for it.
const ScriptedSceneCast* rowOf(std::span<const ScriptedSceneCast> table, std::string_view scene) {
    const auto found = std::ranges::find(table, scene, &ScriptedSceneCast::scene);
    return found != table.end() ? &*found : nullptr;
}

// Entry `index` of a space-separated list; nothing past its end.
std::optional<std::string_view> entryOf(std::string_view list, std::size_t index) {
    std::size_t start = 0;
    for (std::size_t at = 0; start <= list.size() && !list.empty(); ++at) {
        const std::size_t end = std::min(list.find(' ', start), list.size());
        if (at == index) {
            return list.substr(start, end - start);
        }
        start = end + 1;
    }
    return std::nullopt;
}

} // namespace

CastModel castModelOf(std::span<const ScriptedSceneCast> table, std::string_view scene, std::size_t role,
                      std::string_view roleName) {
    CastModel byName{.source = CastSource::Name, .model = standInModelOf(roleName)};
    // A scene no script builds a cast for: the name is all there is.
    const ScriptedSceneCast* row = rowOf(table, scene);
    if (row == nullptr || row->cast.empty()) {
        return byName;
    }
    // The script binds role i to the table's human i + 1 (gPlayCutScene, scenes.md#superrunscene); a role past the
    // table, or bound to NilHandle, is bound to nobody and plays nothing.
    const std::optional<std::string_view> entry = entryOf(row->cast, role);
    if (!entry || *entry == "-") {
        return CastModel{.source = CastSource::Unbound, .model = {}};
    }
    if (*entry == "?") {
        return byName;
    }
    if (*entry == "player") {
        return CastModel{.source = CastSource::Player, .model = {}};
    }
    // Of `a|b`, the first type.
    return CastModel{.source = CastSource::Script, .model = std::string(entry->substr(0, entry->find('|')))};
}

CastModel castModelOf(std::string_view scene, std::size_t role, std::string_view roleName) {
    return castModelOf(scriptedSceneCasts(), scene, role, roleName);
}

std::optional<std::string> propTypeOf(std::span<const ScriptedSceneCast> table, std::string_view scene,
                                      std::size_t slot) {
    // The script binds slot i to the table's object i + 1; an unbound or unresolved slot has no type.
    const ScriptedSceneCast* row = rowOf(table, scene);
    if (row == nullptr) {
        return std::nullopt;
    }
    const std::optional<std::string_view> entry = entryOf(row->props, slot);
    if (!entry || *entry == "-" || *entry == "?") {
        return std::nullopt;
    }
    return std::string(*entry);
}

std::optional<std::string> propTypeOf(std::string_view scene, std::size_t slot) {
    return propTypeOf(scriptedSceneCasts(), scene, slot);
}

std::string standInModelOf(std::string_view roleName) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 12> kModels{{{"warrcl", "warr_cl"},
                                                                                     {"warrve", "warr_ve"},
                                                                                     {"warrrecv", "warr_re_cv"},
                                                                                     {"warrash", "warr_ty"},
                                                                                     {"warrashcv", "warr_ty_cv"},
                                                                                     {"warrlynx", "warr_ly"},
                                                                                     {"warrjones", "warr_jn"},
                                                                                     {"warrmal", "warr_ma"},
                                                                                     {"warrso01", "warr_so_va1"},
                                                                                     {"warrso02", "warr_so_va2"},
                                                                                     {"warrso03", "warr_so_va3"},
                                                                                     {"warrso04", "warr_so_vb1"}}};
    for (const auto& [name, model] : kModels) {
        if (name == roleName) {
            return std::string(model);
        }
    }
    return {};
}

} // namespace coney::scenes
