// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

// The character bindings the story's later missions (8 to 18) add for one human at a time: the killer, double-damage,
// woundable, rubber-neck and grab-block switches of the flag word, the two hearing ranges of its brain, and the
// questions whether it is hidden, fighting, in a scene or a live handle at all. Each acts through the level's
// HumanBindingHost (scripting/human_bindings.h), the scene question through the level's scenes.
// Research: docs/references/bindings/character.md, docs/research/combat.md#human-flags

namespace coney::script {

/// The bindings registered here; installBindings() registers them with addCharacterBindings().
inline constexpr std::array<std::string_view, 12> kCharacterBindings{
    "HuBlockGrab",       "HuBlockRearGrab", "HuInCombat",         "HuIsHidden",      "HuIsInScene",    "HuRubberNeck",
    "HuSetDoubleDamage", "HuSetHearRange",  "HuSetHelpHearRange", "HuSetKillerMode", "HuSetWoundable", "IsAHuman"};

/// Registers kCharacterBindings in `vm`, acting on `context`'s humans (through its AI host's HumanBindingHost; without
/// one the setters do nothing and the questions answer nil), its scenes and its dynamic objects.
///
/// Research: docs/references/bindings/character.md
void addCharacterBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
