// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/character_bindings.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "human/human_flags.h"
#include "scenes/scene_player.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/human_bindings.h"
#include "world_objects/spawn_records.h"

namespace coney::script {

namespace {

// Argument `i` as a handle: truncated to an unsigned integer, as tolua reads it.
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(
        static_cast<std::uint32_t>(static_cast<std::int64_t>(std::trunc(binding::number(args, i)))));
}

// Whether argument `i` is missing or nil.
bool absent(std::span<const Value> args, std::size_t i) { return i >= args.size() || args[i].isNil(); }

// Argument `i` as a boolean: missing, nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (absent(args, i)) {
        return false;
    }
    return args[i].type() != Value::Type::Number || binding::number(args, i) != 0.0;
}

// Argument `i` as a switch only exactly 1 (Lua 4's true) turns on (`HuSetWoundable`).
bool exactlyOne(std::span<const Value> args, std::size_t i) {
    if (absent(args, i)) {
        return false;
    }
    return args[i].type() == Value::Type::Number ? binding::number(args, i) == 1.0 : true;
}

// The level's humans, if there is a level with an AI host.
HumanBindingHost* hostOf(const BindingContext& context) {
    return context.ai != nullptr ? context.ai->humans() : nullptr;
}

// A setter of `bits` of the flag word of the human named by argument 0, on when `on` reads argument 1 as true. The host
// is read at each call: a level gives its brains to a Lua state made before it.
NativeFunction flagSetter(const BindingContext& context, std::uint64_t bits,
                          bool (*on)(std::span<const Value>, std::size_t)) {
    return [context = &context, bits, on](std::span<const Value> args) {
        if (HumanBindingHost* host = hostOf(*context); host != nullptr) {
            host->setFlags(handleArg(args, 0), bits, on(args, 1));
        }
        return binding::none();
    };
}

// A setter of one of the brain's hearing ranges (`help`: the help-call range) of the human named by argument 0.
NativeFunction hearRangeSetter(const BindingContext& context, bool help) {
    return [context = &context, help](std::span<const Value> args) {
        if (HumanBindingHost* host = hostOf(*context); host != nullptr) {
            host->setHearRange(handleArg(args, 0), help, static_cast<float>(binding::number(args, 1)));
        }
        return binding::none();
    };
}

// A question about the human named by argument 0: `read` of its status, nil for a handle that names no human.
template <typename Read> NativeFunction statusQuestion(const BindingContext& context, Read read) {
    return [context = &context, read](std::span<const Value> args) {
        const HumanBindingHost* host = hostOf(*context);
        const std::optional<HumanStatus> status =
            host != nullptr ? host->status(handleArg(args, 0)) : std::optional<HumanStatus>{};
        return binding::boolean(status.has_value() && read(*status));
    };
}

} // namespace

void addCharacterBindings(LuaVm& vm, const BindingContext& context) {
    // The flag word's switches (docs/research/combat/index.md#human-flags).
    // `HuSetKillerMode(human, on)`: bit 0x20, every strike takes all the victim's health (human::Fighter::applyHit()).
    // @orig 0x0035b980 HuSetKillerMode (unknown)
    // @orig 0x00235a88 Human_SetKillerMode (unknown)
    vm.registerFunction("HuSetKillerMode", flagSetter(context, human::flag::kKiller, boolArg));
    // `HuSetDoubleDamage(human, on)`: bit 0x4000, hits doubled and through hit armour (human::Fighter::applyHit()).
    // @orig 0x00359a80 HuSetDoubleDamage (unknown)
    // @orig 0x00235e10 Human_SetDoubleDamage (unknown)
    vm.registerFunction("HuSetDoubleDamage", flagSetter(context, human::flag::kDoubleDamage, boolArg));
    // `HuSetWoundable(human, on)`: bit 0x8; only exactly 1 sets it.
    // @orig 0x0035ab00 HuSetWoundable (unknown)
    // @orig 0x002376a0 Human_SetWoundable (unknown)
    vm.registerFunction("HuSetWoundable", flagSetter(context, human::flag::kWoundable, exactlyOne));
    // `HuRubberNeck(human, enable)`: bit 0x1000000.
    // @orig 0x00358b38 HuRubberNeck (unknown)
    // @orig 0x0023a258 Human_SetRubberNeck (unknown)
    vm.registerFunction("HuRubberNeck", flagSetter(context, human::flag::kRubberNeck, boolArg));
    // `HuBlockGrab(human, block)`: both grab-block bits at once.
    // @orig 0x003590e0 HuBlockGrab (unknown)
    // @orig 0x00237a28 Human_BlockGrab (unknown)
    vm.registerFunction("HuBlockGrab",
                        flagSetter(context, human::flag::kBlockFrontGrab | human::flag::kBlockRearGrab, boolArg));
    // `HuBlockRearGrab(human, block)`: the rear grab's bit only.
    // @orig 0x00359140 HuBlockRearGrab (unknown)
    // @orig 0x00237b00 Human_SetBlockRearGrab (unknown)
    vm.registerFunction("HuBlockRearGrab", flagSetter(context, human::flag::kBlockRearGrab, boolArg));

    // The brain's hearing: noises (`+0x134`) and the gang's calls for help (`+0x138`), metres, stored as given.
    // @orig 0x0035c278 HuSetHearRange (unknown)
    // @orig 0x002383e8 Human_SetHearRange (unknown)
    vm.registerFunction("HuSetHearRange", hearRangeSetter(context, false));
    // @orig 0x0035c2f0 HuSetHelpHearRange (unknown)
    // @orig 0x00238430 Human_SetHelpHearRange (unknown)
    vm.registerFunction("HuSetHelpHearRange", hearRangeSetter(context, true));

    // The questions: 1 for true, nil for false or a handle that names no human.
    // `HuIsHidden(human)`: hidden in a shadow (human::Human::hidden(), which the brain's hidden byte follows).
    // @orig 0x0035b758 HuIsHidden (unknown)
    // @orig 0x00235798 Human_IsHidden (unknown)
    vm.registerFunction("HuIsHidden", statusQuestion(context, [](const HumanStatus& s) { return s.hidden; }));
    // `HuInCombat(human)`: an attacker holds one of the attack slots on it, or its brain has a target.
    // @orig 0x0035bfe0 HuInCombat (unknown)
    // @orig 0x00237db8 Human_IsInCombat (unknown)
    // @orig 0x002904a0 Brain_IsEngaged (unknown)
    vm.registerFunction("HuInCombat", statusQuestion(context, [](const HumanStatus& s) { return s.inCombat; }));
    // `HuIsInScene(human)`: it holds a role in a starting or playing scene (its scene id, or the in-scene state the
    // scene player gives it as the scene starts).
    // @orig 0x0035b668 HuIsInScene (unknown)
    // @orig 0x002356c8 Human_IsInScene (unknown)
    vm.registerFunction("HuIsInScene", [context = &context](std::span<const Value> args) {
        return binding::boolean(context->scenes != nullptr && context->scenes->inScene(handleArg(args, 0)));
    });
    // `IsAHuman(handle)`: whether the handle still names something live, of any kind: a human or a dynamic object.
    // **Coney's reading** of the shared handle table: Coney keeps no one table, so the humans' and the dynamic
    // objects' are asked; the level's fixed objects (glass, doors) are not.
    // @orig 0x0036cb70 IsAHuman (unknown)
    // @orig 0x00385b90 IsHumanHandle (unknown)
    vm.registerFunction("IsAHuman", [context = &context](std::span<const Value> args) {
        const double handle = handleArg(args, 0);
        const HumanBindingHost* host = hostOf(*context);
        const bool human = host != nullptr && host->status(handle).has_value();
        const world_objects::SpawnRecord* record =
            context->spawnRecords != nullptr ? context->spawnRecords->find(handle) : nullptr;
        const bool object = record != nullptr && !record->removed;
        return binding::boolean(handle != 0.0 && (human || object));
    });
}

} // namespace coney::script
