// SPDX-License-Identifier: GPL-3.0-or-later
// The native list: every binding a fresh state gets, as data independent of any VM (name, origin, status, whether a
// mod may call it, the call), so the runtime installs it and the debug menus and the SDK read the same thing
// (docs/guides/asset-and-script-architecture.md#script-parts, docs/guides/studio/lua-sdk.md#availability).
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "scripting/lua_value.h"

namespace coney::script {

class LuaVm;

/// How far Coney implements a binding.
enum class BindingKind : std::uint8_t {
    Real,   ///< Does the binding's job.
    Routed, ///< Hands the request to a Coney stand-in (music, movies, the Rumble mode menus), which logs it.
    Stub,   ///< Does nothing but return its documented default: its subsystem does not exist in Coney yet.
};

/// Where a native comes from.
enum class NativeOrigin : std::uint8_t {
    Original, ///< One of the original's 956 bindings (docs/research/scripting.md#bindings).
    Coney,    ///< Coney's own, in the `Coney` table; none exist yet
              ///< (asset-and-script-architecture.md#coney-native-candidates).
};

/// One native, independent of any VM: what the runtime installs and the debug menus list.
struct Native {
    std::string name;                             ///< The global a script calls (`HuCreate`).
    NativeOrigin origin = NativeOrigin::Original; ///< The original's or Coney's.
    BindingKind status = BindingKind::Stub;       ///< How far Coney implements it (the binding table's kind).
    bool mods = true;                             ///< Whether a mod project may call it (allowedForMods()).
    NativeFunction call;                          ///< The function a script's call runs.
};

/// The natives being registered for a fresh state, in the order they were first registered. Registration code adds
/// to it as it once set globals on the VM: a name added again keeps its place and takes the new call, as a global set
/// twice does, so installing the list gives the state exactly the globals the old registration gave.
class NativeList {
  public:
    /// Registers `call` as the native `name` (status, origin and the mods flag are filled in by buildNatives()).
    void add(std::string_view name, NativeFunction call);
    /// The natives, in registration order.
    [[nodiscard]] std::span<const Native> natives() const { return m_natives; }
    /// The natives, in registration order, for buildNatives() to complete.
    [[nodiscard]] std::span<Native> natives() { return m_natives; }
    /// Takes the natives out of the list, which is left empty.
    [[nodiscard]] std::vector<Native> take();

  private:
    std::vector<Native> m_natives;
    std::map<std::string, std::size_t, std::less<>> m_index; // name -> position in m_natives
};

/// Makes each native of `natives` callable in `vm` as the global of its name, in the list's order.
void installNatives(LuaVm& vm, std::span<const Native> natives);

/// The native `name` of `natives`; null when there is none.
[[nodiscard]] const Native* findNative(std::span<const Native> natives, std::string_view name);

/// Whether a mod project may call the game native `name`: all but the few that belong to the game's own scripts
/// (they run a disc script into the calling state, collect its garbage, or hold one of the game flow's one-slot
/// callbacks): docs/guides/studio/lua-sdk.md#availability, item 3.
[[nodiscard]] bool allowedForMods(std::string_view name);

} // namespace coney::script
