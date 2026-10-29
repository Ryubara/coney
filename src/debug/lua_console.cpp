// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/lua_console.h"

#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <utility>

#include "debug/native_caller.h"
#include "scripting/lua_compiler.h"

namespace coney::debug {

namespace {

using script::Value;

// The chunk name of a console line: errors show it as `console:<line>:`.
constexpr std::string_view kConsoleChunk = "=console";

// The one line a run prints: `= ` and its values, or nothing when it has none.
std::vector<std::string> printed(const std::vector<Value>& values) {
    if (values.empty()) {
        return {};
    }
    std::string line = "=";
    for (std::size_t i = 0; i < values.size(); ++i) {
        line += (i == 0 ? " " : ", ") + formatValue(values[i]);
    }
    return {line};
}

// Compiles a console line: `=expr` and an expression as `return ...`, so their values print, as Lua's own console
// does; anything else as the statements it is. A line that is neither reports the error of reading it as statements.
std::expected<std::shared_ptr<const script::LuaProto>, Error> compileLine(std::string_view line) {
    if (line.starts_with('=')) {
        return script::loadLuaSource(std::format("return {}", line.substr(1)), kConsoleChunk);
    }
    if (auto expression = script::loadLuaSource(std::format("return {}", line), kConsoleChunk)) {
        return expression;
    }
    return script::loadLuaSource(line, kConsoleChunk);
}

// Runs `chunk` in `vm` and returns the line its results print.
std::expected<std::vector<std::string>, Error> runChunk(script::LuaVm& vm,
                                                        std::shared_ptr<const script::LuaProto> chunk) {
    auto results = vm.run(std::move(chunk));
    if (!results) {
        return std::unexpected(std::move(results.error()));
    }
    return printed(*results);
}

} // namespace

LuaConsole::LuaConsole() : m_history(std::make_shared<std::vector<std::string>>()) {}

void LuaConsole::remember(std::string_view line) {
    if (line.empty() || (!m_history->empty() && m_history->back() == line)) {
        return;
    }
    m_history->emplace_back(line);
    if (m_history->size() > kHistoryLength) {
        m_history->erase(m_history->begin());
    }
}

std::expected<std::vector<std::string>, Error> LuaConsole::run(script::LuaVm& vm, std::string_view line) {
    remember(line);
    auto chunk = compileLine(line);
    if (!chunk) {
        return std::unexpected(std::move(chunk.error()));
    }
    return runChunk(vm, std::move(*chunk));
}

std::expected<std::vector<std::string>, Error> LuaConsole::runFile(script::LuaVm& vm, const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("{}: cannot be read", path));
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    // A compiled chunk or Lua source, told apart as Lua's dofile does.
    auto chunk = script::loadLuaBuffer(std::as_bytes(std::span(text)), "@" + path);
    if (!chunk) {
        return std::unexpected(std::move(chunk.error()));
    }
    auto lines = runChunk(vm, std::move(*chunk));
    if (lines && lines->empty()) {
        lines->push_back(std::format("{}: ran", path));
    }
    return lines;
}

std::expected<std::vector<script::Value>, Error> evaluateLua(script::LuaVm& vm, std::string_view text) {
    auto chunk = script::loadLuaSource(std::format("return {}", text), kConsoleChunk);
    if (!chunk) {
        return std::unexpected(std::move(chunk.error()));
    }
    return vm.run(std::move(*chunk));
}

} // namespace coney::debug
