// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/lua_compiler.h"

namespace coney::script {

std::expected<LuaProto, Error> compileLua(std::string_view, std::string_view) {
    return fail(ErrorCode::Invalid, "not written yet");
}

std::expected<std::shared_ptr<const LuaProto>, Error> loadLuaSource(std::string_view, std::string_view) {
    return fail(ErrorCode::Invalid, "not written yet");
}

std::expected<std::shared_ptr<const LuaProto>, Error> loadLuaBuffer(std::span<const std::byte>, std::string_view) {
    return fail(ErrorCode::Invalid, "not written yet");
}

std::string luaChunkLabel(std::string_view) { return {}; }

} // namespace coney::script
