// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the `--scene` preview's cast; the original only plays a scene with the humans and objects a script binds
// What Coney's scene preview (`--scene`, docs/guides/running-play-level.md#playing-a-level) binds to a scene's roles
// and object slots. The original never turns a role's or a slot's name into a model: a role draws the human the script
// binds to it, so the model is that human's character type, and a slot moves the object the script binds, of its
// object type (Scene_BindHuman 0x003541a0 and Scene_BindObject 0x00354280 bind by index only; docs/research/scenes.md
// #cast, confirmed at runtime for `l99_c1` and the subway scenes, #cast-runtime). The preview plays a scene with no
// script, so it takes those bindings from the scripts' scene tables as research/references/scenes.yaml records them
// (its Cast and Props), and falls back to the role's name only for a scene no script plays.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace coney::scenes {

/// One scene a script builds a scene table for, as research/references/scenes.yaml records it: the scene's name, then
/// its Cast and Props, one entry per role or object slot in order, separated by spaces. An entry is a character model
/// or object type, `a|b` when the script makes the human as either type, `player` for the player, `-` for a role or
/// slot the script leaves unbound and `?` for one the scripts' walk did not resolve (and for a door's leaf, which is
/// no object type the preview can make).
struct ScriptedSceneCast {
    std::string_view scene;
    std::string_view cast;
    std::string_view props;
};

/// Every scene with a Cast or Props in research/references/scenes.yaml, written by `coney-tools refs render`
/// (scene_casts_table.cpp).
[[nodiscard]] std::span<const ScriptedSceneCast> scriptedSceneCasts();

/// Where the preview's model for a role comes from.
enum class CastSource : std::uint8_t {
    Script,  ///< The character type of the human a script binds to the role (the scene table's Cast).
    Player,  ///< A script binds the player: the play mode draws him.
    Unbound, ///< A script plays the scene and binds nobody to the role (`-`, or past its table): it plays nothing.
    Name,    ///< No script plays the scene, or its binding is not resolved: a stand-in from the role's name.
};

/// The model the preview draws a role as, and where it comes from. `model` is empty for CastSource::Player and
/// CastSource::Unbound, and for a CastSource::Name whose name stands for no model.
struct CastModel {
    CastSource source = CastSource::Name;
    std::string model;
};

/// The model role `role` (named `roleName`) of scene `scene` is drawn as in the preview, from `table`: the scene
/// table's binding first, the role's name when the scene has no Cast or the role's binding is `?`. Of two or more
/// types (`a|b`) the first is taken (**Coney's choice**: which one a level's checkpoint makes is open,
/// docs/research/scenes.md#open-questions).
[[nodiscard]] CastModel castModelOf(std::span<const ScriptedSceneCast> table, std::string_view scene, std::size_t role,
                                    std::string_view roleName);
/// castModelOf() over scriptedSceneCasts().
[[nodiscard]] CastModel castModelOf(std::string_view scene, std::size_t role, std::string_view roleName);

/// The object type the preview binds to object slot `slot` of scene `scene`, from `table`'s Props; nothing for a scene
/// with no Props, a slot left unbound (`-`), unresolved (`?`) or past its table. An object's name is never its type
/// (`Coney_Sub2`'s `s_train_low` moves a `dyn_s_train_med_full` car), so there is no fallback from the name.
[[nodiscard]] std::optional<std::string> propTypeOf(std::span<const ScriptedSceneCast> table, std::string_view scene,
                                                    std::size_t slot);
/// propTypeOf() over scriptedSceneCasts().
[[nodiscard]] std::optional<std::string> propTypeOf(std::string_view scene, std::size_t slot);

/// **Coney stand-in** for a scene no script plays: the model a role's name stands for in level99's scenes (a role names
/// its cutscene model, not a Character List entry); empty for a name it does not know.
[[nodiscard]] std::string standInModelOf(std::string_view roleName);

} // namespace coney::scenes
