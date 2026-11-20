// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the `--scene` preview's cast; the original only plays a scene with the humans and objects a script binds
#include "scenes/scene_casts.h"

#include <array>
#include <utility>

namespace coney::scenes {

CastModel castModelOf(std::span<const ScriptedSceneCast> /*table*/, std::string_view /*scene*/, std::size_t /*role*/,
                      std::string_view roleName) {
    return CastModel{.source = CastSource::Name, .model = standInModelOf(roleName)};
}

CastModel castModelOf(std::string_view scene, std::size_t role, std::string_view roleName) {
    return castModelOf(scriptedSceneCasts(), scene, role, roleName);
}

std::optional<std::string> propTypeOf(std::span<const ScriptedSceneCast> /*table*/, std::string_view /*scene*/,
                                      std::size_t /*slot*/) {
    return std::nullopt;
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
