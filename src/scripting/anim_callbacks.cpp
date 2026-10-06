// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/anim_callbacks.h"

#include <cmath>
#include <span>
#include <utility>

#include "scripting/binding_args.h"

namespace coney::script {

bool AnimCallbacks::resolves(double handle) const { return handle != 0.0 && m_resolves && m_resolves(handle); }

bool AnimCallbacks::add(double human, std::uint32_t anim, std::string_view name) {
    for (Slot& slot : m_slots) {
        // The all-humans flag is not tested: an all-humans slot (whose handle never resolves) can be taken.
        if (!resolves(slot.human)) {
            slot = Slot{.human = human, .anim = anim, .allHumans = false, .name = std::string(name)};
            return true;
        }
    }
    return false;
}

bool AnimCallbacks::addAll(std::uint32_t anim, std::string_view name) {
    for (Slot& slot : m_slots) {
        if (!resolves(slot.human) && !slot.allHumans) {
            slot = Slot{.human = 0.0, .anim = anim, .allHumans = true, .name = std::string(name)};
            return true;
        }
    }
    return false;
}

void AnimCallbacks::remove(double human, std::uint32_t anim) {
    for (Slot& slot : m_slots) {
        if (!slot.name.empty() && slot.anim == anim && (slot.allHumans || slot.human == human)) {
            slot = Slot{};
            return;
        }
    }
}

std::string_view AnimCallbacks::match(double human, std::uint32_t anim) const {
    for (const Slot& slot : m_slots) {
        if (slot.name.empty() || slot.anim != anim) {
            continue;
        }
        if (slot.allHumans || (slot.human == human && resolves(human))) {
            return slot.name;
        }
    }
    return {};
}

namespace {

// Argument `i` truncated to an unsigned whole number, as tolua reads one.
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    const double value = std::trunc(binding::number(args, i));
    return value > 0.0 ? static_cast<std::uint32_t>(value) : 0U;
}

} // namespace

void addAnimCallbackBindings(LuaVm& vm, const BindingContext& context) {
    // `AddAnimCallback(human, anim, name)` -> true, or nil when the table is full.
    vm.registerFunction("AddAnimCallback", [table = context.animCallbacks](std::span<const Value> args) {
        const bool added = table != nullptr && table->add(static_cast<double>(unsignedArg(args, 0)),
                                                          unsignedArg(args, 1), binding::string(args, 2));
        return binding::boolean(added);
    });
    // `AddAllAnimCallback(anim, name)` -> true, or nil when no slot is free.
    vm.registerFunction("AddAllAnimCallback", [table = context.animCallbacks](std::span<const Value> args) {
        const bool added = table != nullptr && table->addAll(unsignedArg(args, 0), binding::string(args, 1));
        return binding::boolean(added);
    });
    // `DelAnimCallback(human, anim)`.
    vm.registerFunction("DelAnimCallback", [table = context.animCallbacks](std::span<const Value> args) {
        if (table != nullptr) {
            table->remove(static_cast<double>(unsignedArg(args, 0)), unsignedArg(args, 1));
        }
        return binding::none();
    });
}

} // namespace coney::script
