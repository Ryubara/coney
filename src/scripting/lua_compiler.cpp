// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only Coney's own compiler; the original links stock Lua 4.0.1's parser (0x0032d828), which is not ported
#include "scripting/lua_compiler.h"

#include <format>
#include <utility>

#include "scripting/lua_parser.h"

namespace coney::script {

namespace {

// How much of a source text a `[string "..."]` label shows.
constexpr std::size_t kLabelLength = 40;

} // namespace

std::expected<LuaProto, Error> compileLua(std::string_view source, std::string_view chunkName) {
    LuaParser parser(source, std::string(chunkName), luaChunkLabel(chunkName));
    return parser.compile();
}

std::expected<std::shared_ptr<const LuaProto>, Error> loadLuaSource(std::string_view source,
                                                                    std::string_view chunkName) {
    auto proto = compileLua(source, chunkName);
    if (!proto) {
        return std::unexpected(std::move(proto.error()));
    }
    return std::make_shared<const LuaProto>(std::move(*proto));
}

std::expected<std::shared_ptr<const LuaProto>, Error> loadLuaBuffer(std::span<const std::byte> buffer,
                                                                    std::string_view chunkName) {
    // Lua 4.0 tells a precompiled chunk from source by its first byte, the ESC of the signature "\x1bLua".
    if (!buffer.empty() && buffer.front() == std::byte{0x1b}) {
        auto proto = parseLuaChunk(buffer);
        if (!proto) {
            return std::unexpected(std::move(proto.error()));
        }
        return std::make_shared<const LuaProto>(std::move(*proto));
    }
    const std::string_view text(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    return loadLuaSource(text, chunkName);
}

std::string luaChunkLabel(std::string_view chunkName) {
    if (chunkName.starts_with('=') || chunkName.starts_with('@')) {
        return std::string(chunkName.substr(1));
    }
    // Source used as its own name: its first line, cut short, marked as cut when anything follows.
    const std::size_t lineEnd = chunkName.find('\n');
    std::string_view shown = chunkName.substr(0, lineEnd);
    bool cut = lineEnd != std::string_view::npos;
    if (shown.size() > kLabelLength) {
        shown = shown.substr(0, kLabelLength);
        cut = true;
    }
    return std::format("[string \"{}{}\"]", shown, cut ? "..." : "");
}

} // namespace coney::script
