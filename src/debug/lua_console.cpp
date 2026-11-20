// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/lua_console.h"

#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "debug/native_caller.h"
#include "scripting/lua_compiler.h"
#include "scripting/lua_libraries.h"

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

// Points the global `print` at a console's sink for one run and puts the state's own back with restore() after it,
// unless the run set `print` itself (a line such as `print = ToInt` keeps what it set). The put-back is a call, not
// a destructor: it allocates, and a destructor must not throw.
// @coney-only the debug console's output: the original has no console, and its print goes nowhere a player sees
class PrintRedirect {
  public:
    // Sends `print` in `vm` to `sink`; an empty sink changes nothing.
    PrintRedirect(script::LuaVm& vm, const LuaConsole::PrintSink& sink) : m_vm(vm) {
        if (!sink) {
            return;
        }
        m_saved = vm.global("print");
        auto function = std::make_shared<script::Function>();
        function->native = [sink](std::span<const Value> args) -> std::expected<std::vector<Value>, Error> {
            // The line Lua 4.0's print writes: each argument's tostring, joined by tabs.
            std::string line;
            for (std::size_t i = 0; i < args.size(); ++i) {
                line += (i == 0 ? "" : "\t") + script::luaToString(args[i]);
            }
            sink(std::move(line));
            return std::vector<Value>{};
        };
        m_redirect = Value(std::shared_ptr<const script::Function>(std::move(function)));
        vm.setGlobal("print", m_redirect);
    }
    // Puts the state's `print` back if the run left the console's in place.
    void restore() {
        if (!m_redirect.isNil() && m_vm.global("print") == m_redirect) {
            m_vm.setGlobal("print", m_saved);
        }
    }
    PrintRedirect(const PrintRedirect&) = delete;
    PrintRedirect& operator=(const PrintRedirect&) = delete;
    PrintRedirect(PrintRedirect&&) = delete;
    PrintRedirect& operator=(PrintRedirect&&) = delete;

  private:
    script::LuaVm& m_vm;
    Value m_saved;    // the state's own print
    Value m_redirect; // the console's; nil when there is no sink
};

// Runs `chunk` in `vm`, with `print` going to `print` when given, and returns the line its results print.
std::expected<std::vector<std::string>, Error>
runChunk(script::LuaVm& vm, std::shared_ptr<const script::LuaProto> chunk, const LuaConsole::PrintSink& print) {
    PrintRedirect redirect(vm, print);
    auto results = vm.run(std::move(chunk));
    redirect.restore();
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

std::expected<std::vector<std::string>, Error> LuaConsole::run(script::LuaVm& vm, std::string_view line,
                                                               const PrintSink& print) {
    remember(line);
    auto chunk = compileLine(line);
    if (!chunk) {
        return std::unexpected(std::move(chunk.error()));
    }
    return runChunk(vm, std::move(*chunk), print);
}

std::expected<std::vector<std::string>, Error> LuaConsole::runFile(script::LuaVm& vm, const std::string& path,
                                                                   const PrintSink& print) {
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
    auto lines = runChunk(vm, std::move(*chunk), print);
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
