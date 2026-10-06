// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The scripts' animation callbacks (`AddAnimCallback`, `AddAllAnimCallback`, `DelAnimCallback`): a Lua function per
/// human and anim id, called with (human, anim id) when that human starts playing the id. A table of 16 slots, the
/// first match firing.
///
/// Research: docs/research/characters.md#anim-callbacks, docs/references/bindings/character.md#addanimcallback
class AnimCallbacks {
  public:
    /// The table's slots (`0x006b6710`, 16 of 16 bytes).
    static constexpr std::size_t kSlots = 16;
    /// Whether a handle names a live human: a slot whose handle does not is free.
    using Resolves = std::function<bool(double handle)>;

    /// One slot.
    struct Slot {
        double human = 0; ///< The human's handle; 0 (NilHandle) for a free or all-humans slot.
        std::uint32_t anim = 0;
        bool allHumans = false; ///< Set by `AddAllAnimCallback`: the slot matches any human.
        std::string name;       ///< The Lua function; empty for a slot never used.
    };

    /// Sets how handles are resolved; with none, no handle resolves (every slot of a human is free).
    void setResolves(Resolves resolves) { m_resolves = std::move(resolves); }

    /// `AddAnimCallback(human, anim, name)`: the first slot whose handle does not resolve (an all-humans one too) takes
    /// it. Returns false when every slot is taken.
    /// @orig 0x0023a9f8 AnimCallback_Add (unknown)
    bool add(double human, std::uint32_t anim, std::string_view name);
    /// `AddAllAnimCallback(anim, name)`: the first slot whose handle does not resolve and that is not already an
    /// all-humans one, marked all-humans. Returns false when there is none.
    /// @orig 0x0023aab0 AnimCallback_AddForAll (unknown)
    bool addAll(std::uint32_t anim, std::string_view name);
    /// `DelAnimCallback(human, anim)`: frees the first slot of `anim` that is all-humans or `human`'s.
    /// @orig 0x0023ab80 AnimCallback_Remove (unknown)
    void remove(double human, std::uint32_t anim);
    /// Empties every slot (`InitLevel`).
    /// @orig 0x0023a9b8 AnimCallback_Clear (unknown)
    void clear() { m_slots = {}; }

    /// The function the first slot matching `human` starting `anim` names (its id equal, and all-humans or the
    /// human's); empty for none.
    /// @orig 0x0023ac50 AnimCallback_Dispatch (unknown)
    [[nodiscard]] std::string_view match(double human, std::uint32_t anim) const;
    [[nodiscard]] const std::array<Slot, kSlots>& slots() const { return m_slots; }

  private:
    // Whether `handle` names a live human.
    [[nodiscard]] bool resolves(double handle) const;

    std::array<Slot, kSlots> m_slots{};
    Resolves m_resolves;
};

/// The animation callback bindings, working on `context`'s table (null keeps none).
inline constexpr std::array<std::string_view, 3> kAnimCallbackBindings{"AddAllAnimCallback", "AddAnimCallback",
                                                                       "DelAnimCallback"};

/// Registers kAnimCallbackBindings in `vm`, working on `context.animCallbacks`.
void addAnimCallbackBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
