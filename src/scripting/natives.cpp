// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the test commit: the native list is not built yet.
#include "scripting/natives.h"

#include "scripting/lua_vm.h"

namespace coney::script {

void NativeList::add(std::string_view /*name*/, NativeFunction /*call*/) {}

std::vector<Native> NativeList::take() { return {}; }

void installNatives(LuaVm& /*vm*/, std::span<const Native> /*natives*/) {}

const Native* findNative(std::span<const Native> /*natives*/, std::string_view /*name*/) { return nullptr; }

bool allowedForMods(std::string_view /*name*/) { return true; }

} // namespace coney::script
