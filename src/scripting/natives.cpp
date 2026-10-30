// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the native list, Coney's seam between the bindings and the runtime; the original registers on tolua
#include "scripting/natives.h"

#include <algorithm>
#include <array>
#include <utility>

#include "scripting/lua_vm.h"

namespace coney::script {

namespace {

// The game natives a mod project may not call, from the SDK design's first list (docs/guides/studio/lua-sdk.md,
// "How a native reaches the SDK", item 3): they run a disc script into the calling state (a project uses `require`),
// collect the state's garbage (a project's state collects its own), or set one of the game flow's one-slot callbacks,
// which belong to the game's own scripts.
constexpr std::array<std::string_view, 11> kNotForMods{
    "doFile",
    "doInclude",
    "preLoadFile",
    "gc",
    "RegisterUpdate",
    "SetStartGameCallback",
    "SetCheatCallback",
    "SetMultiplayerCallback",
    "SetArmiesMultiplayerCallback",
    "SetAllClearCallBack",
    "SetHatCallBack",
};

} // namespace

void NativeList::add(std::string_view name, NativeFunction call) {
    // A name registered again keeps its place, as a VM global set twice keeps its slot.
    if (const auto found = m_index.find(name); found != m_index.end()) {
        m_natives[found->second].call = std::move(call);
        return;
    }
    m_index.emplace(std::string(name), m_natives.size());
    Native native;
    native.name = std::string(name);
    native.call = std::move(call);
    m_natives.push_back(std::move(native));
}

std::vector<Native> NativeList::take() {
    m_index.clear();
    return std::exchange(m_natives, {});
}

void installNatives(LuaVm& vm, std::span<const Native> natives) {
    // The calls are copied into the VM: the bindings keep their state behind pointers (the context, the handle
    // counter), so the list's copy and the VM's act on the same things.
    for (const Native& native : natives) {
        vm.registerFunction(native.name, native.call);
    }
}

const Native* findNative(std::span<const Native> natives, std::string_view name) {
    const auto found = std::ranges::find(natives, name, &Native::name);
    return found == natives.end() ? nullptr : &*found;
}

bool allowedForMods(std::string_view name) { return std::ranges::find(kNotForMods, name) == kNotForMods.end(); }

} // namespace coney::script
