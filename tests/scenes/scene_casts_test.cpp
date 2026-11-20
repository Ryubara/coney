// SPDX-License-Identifier: GPL-3.0-or-later
// The scene preview's cast (src/scenes/scene_casts.h): a role draws the character type the scene's script binds to it
// and a slot the object type, as research/references/scenes.yaml records them, and only a scene no script plays falls
// back to the role's name (docs/research/scenes.md#cast).

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "scenes/scene_casts.h"

namespace scenes = coney::scenes;

namespace {

// A synthetic table: one scene with every kind of cast entry, one with props only.
constexpr std::array<scenes::ScriptedSceneCast, 2> kTable{{
    {.scene = "t1_c1", .cast = "civl_a player - ? warr_ve|warr_ve_cv", .props = "dyn_box - ? dyn_crate"},
    {.scene = "t1_sub", .cast = "", .props = "dyn_car dyn_car"},
}};

} // namespace

TEST_CASE("the scene preview casts a role from its scene's script binding before its name", "[scenes][casts]") {
    // Expected: scenes.md#cast, confirmed (code): a role draws the human bound by index, so its character type
    // Expected: scenes.md#coneys-implementation, Coney's own preview: `-` unbound, `?` the name, `a|b` the first
    // Bound to a character type: the type, whatever the role is called.
    scenes::CastModel cast = scenes::castModelOf(kTable, "t1_c1", 0, "warrcl");
    CHECK(cast.source == scenes::CastSource::Script);
    CHECK(cast.model == "civl_a");
    // The player: the play mode draws him.
    cast = scenes::castModelOf(kTable, "t1_c1", 1, "warrrecv");
    CHECK(cast.source == scenes::CastSource::Player);
    CHECK(cast.model.empty());
    // Bound to nobody (`-`) or past the table: the role plays nothing, even with a name the stand-in knows.
    cast = scenes::castModelOf(kTable, "t1_c1", 2, "warrcl");
    CHECK(cast.source == scenes::CastSource::Unbound);
    CHECK(cast.model.empty());
    cast = scenes::castModelOf(kTable, "t1_c1", 5, "warrcl");
    CHECK(cast.source == scenes::CastSource::Unbound);
    // Not resolved (`?`): the name's stand-in.
    cast = scenes::castModelOf(kTable, "t1_c1", 3, "warrcl");
    CHECK(cast.source == scenes::CastSource::Name);
    CHECK(cast.model == "warr_cl");
    // Either of two types: the first.
    cast = scenes::castModelOf(kTable, "t1_c1", 4, "warrve");
    CHECK(cast.source == scenes::CastSource::Script);
    CHECK(cast.model == "warr_ve");
}

TEST_CASE("the scene preview binds a slot to the object type its scene's script binds", "[scenes][casts]") {
    // Expected: scenes.md#cast, confirmed (code): a slot moves the object bound by index, so its object type
    CHECK(scenes::propTypeOf(kTable, "t1_c1", 0) == std::optional<std::string>("dyn_box"));
    CHECK(scenes::propTypeOf(kTable, "t1_c1", 3) == std::optional<std::string>("dyn_crate"));
    CHECK(scenes::propTypeOf(kTable, "t1_sub", 1) == std::optional<std::string>("dyn_car"));
    // Unbound, unresolved, past the table, or a scene with no Props: nothing.
    CHECK_FALSE(scenes::propTypeOf(kTable, "t1_c1", 1).has_value());
    CHECK_FALSE(scenes::propTypeOf(kTable, "t1_c1", 2).has_value());
    CHECK_FALSE(scenes::propTypeOf(kTable, "t1_c1", 4).has_value());
    CHECK_FALSE(scenes::propTypeOf(kTable, "no_such", 0).has_value());
}

TEST_CASE("the scene preview casts from a role's name only for a scene no script plays", "[scenes][casts]") {
    // Expected: scenes.md#coneys-implementation, Coney's own preview: the level99 role names' stand-ins
    // No such scene in the table, or a scene with props but no cast: the name's stand-in, empty for an unknown name.
    scenes::CastModel cast = scenes::castModelOf(kTable, "no_such", 0, "warrashcv");
    CHECK(cast.source == scenes::CastSource::Name);
    CHECK(cast.model == "warr_ty_cv");
    cast = scenes::castModelOf(kTable, "t1_sub", 0, "warrcl");
    CHECK(cast.source == scenes::CastSource::Name);
    CHECK(cast.model == "warr_cl");
    cast = scenes::castModelOf(kTable, "no_such", 0, "sanchez");
    CHECK(cast.source == scenes::CastSource::Name);
    CHECK(cast.model.empty());
}

TEST_CASE("the preview's l99_c1 and subway scenes take the cast and props read at runtime", "[scenes][casts]") {
    // Expected: scenes.md#cast-runtime, confirmed (runtime)
    // docs/research/scenes.md#cast-runtime (confirmed at runtime, PCSX2, 2026-10-11): l99_c1's roles are the bound
    // humans' types, Ash (role 7, `warrashcv`) type 40, `warr_ty_cv`; Vermin (role 1) `warr_ve_gen`, not the name's
    // `warr_ve`; role 3 is bound to nobody.
    scenes::CastModel ash = scenes::castModelOf("l99_c1", 7, "warrashcv");
    CHECK(ash.source == scenes::CastSource::Script);
    CHECK(ash.model == "warr_ty_cv");
    scenes::CastModel vermin = scenes::castModelOf("l99_c1", 1, "warrve");
    CHECK(vermin.source == scenes::CastSource::Script);
    CHECK(vermin.model == "warr_ve_gen");
    CHECK(scenes::castModelOf("l99_c1", 3, "warrso01").source == scenes::CastSource::Unbound);
    // Its slot `cleon` is the intro card's type, not the level's `dyn_cleon`.
    CHECK(scenes::propTypeOf("l99_c1", 3) == std::optional<std::string>("dyn_intro_test"));
    // The subway scenes' slots 0-2 and 4-6 move `dyn_s_train_med_full` cars, whatever their names; slot 3,
    // `train_sound`, has no object type.
    for (const char* scene : {"Coney_Sub2", "Coney_Sub4"}) {
        INFO(scene);
        for (std::size_t slot : {0U, 1U, 2U, 4U, 5U, 6U}) {
            CHECK(scenes::propTypeOf(scene, slot) == std::optional<std::string>("dyn_s_train_med_full"));
        }
        CHECK_FALSE(scenes::propTypeOf(scene, 3).has_value());
    }
}
