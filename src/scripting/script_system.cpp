// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/script_system.h"

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "scripting/lua_libraries.h"

namespace coney::script {

namespace {

// The Lua stack the original opens its state with (0x400 slots); Coney's VM grows its stack as needed, so it is the
// call depth that bounds a runaway script instead.
constexpr int kMaxCallDepth = 200;

// A function that does nothing: the original's `_ERRORMESSAGE` and `_ALERT`.
std::expected<std::vector<Value>, Error> silent(std::span<const Value> /*args*/) { return std::vector<Value>{}; }

// One value as a trace line shows it: numbers in short form, strings quoted, tables and functions by kind.
std::string traced(const Value& value) {
    switch (value.type()) {
    case Value::Type::Nil:
        return "nil";
    case Value::Type::Number:
        return std::format("{:g}", value.number().value_or(0.0));
    case Value::Type::String:
        return std::format("\"{}\"", value.string().value_or(std::string_view{}));
    case Value::Type::Table: {
        // A short list of numbers and strings (a position, a colour) is shown whole; anything else by kind.
        constexpr int kShown = 8;
        std::string text;
        int shown = 0;
        const Table& table = *value.table();
        for (auto entry = table.next(Value()); entry; entry = table.next(entry->first)) {
            const Value::Type type = entry->second.type();
            if (++shown > kShown || (type != Value::Type::Number && type != Value::Type::String)) {
                return "{table}";
            }
            const std::string item = entry->first.number()
                                         ? traced(entry->second)
                                         : std::format("{}={}", traced(entry->first), traced(entry->second));
            text += text.empty() ? item : std::format(", {}", item);
        }
        return std::format("{{{}}}", text);
    }
    case Value::Type::Function:
        return "function";
    }
    return "?";
}

// A call's arguments (or results), comma separated, as a trace line shows them.
std::string tracedArgs(std::span<const Value> args) {
    std::string text;
    for (const Value& arg : args) {
        text += text.empty() ? traced(arg) : std::format(", {}", traced(arg));
    }
    return text;
}

} // namespace

ScriptSystem::ScriptSystem(ScriptSource source, BindingInstaller install, Log log)
    : m_source(std::move(source)), m_install(std::move(install)), m_log(std::move(log)) {}

void ScriptSystem::create() {
    if (m_vm) {
        destroy();
    }
    LuaVmOptions options;
    // A call of an unset global is a binding Coney lacks (the original registers all 956): skip it, see the class
    // comment.
    options.nilCallsAreNoOps = true;
    options.maxCallDepth = kMaxCallDepth;
    m_vm = std::make_unique<LuaVm>(options);
    ++m_generation;

    // The libraries in the original's order: string, base, math; then the bindings, which may replace a library
    // function (`random`).
    openStringLibrary(*m_vm);
    BaseLibraryHooks hooks;
    hooks.doFile = [this](std::string_view name) -> std::expected<std::vector<Value>, Error> {
        runFile(name);
        return std::vector<Value>{};
    };
    hooks.print = [this](std::string_view line) { log(std::format("script print: {}", line)); };
    openBaseLibrary(*m_vm, std::move(hooks));
    openMathLibrary(*m_vm);
    if (m_install) {
        m_install(*this, *m_vm);
    }
    // Errors leave no trace in the game: both handlers do nothing. Coney logs errors itself (reportError()).
    m_vm->registerFunction("_ERRORMESSAGE", silent);
    m_vm->registerFunction("_ALERT", silent);
    if (m_trace) {
        wrapBindingsForTrace();
    }
}

void ScriptSystem::traceCalls(Log trace) {
    m_trace = trace ? std::make_shared<Log>(std::move(trace)) : nullptr;
    if (m_vm && m_trace) {
        wrapBindingsForTrace();
    }
}

void ScriptSystem::wrapBindingsForTrace() {
    // Collect first: setting globals while walking them would disturb the walk.
    std::vector<std::pair<std::string, std::shared_ptr<const Function>>> natives;
    Table& globals = m_vm->globals();
    for (auto entry = globals.next(Value()); entry; entry = globals.next(entry->first)) {
        const std::shared_ptr<const Function>& function = entry->second.function();
        const std::optional<std::string_view> name = entry->first.string();
        if (function && function->native && name && *name != "_ERRORMESSAGE" && *name != "_ALERT") {
            natives.emplace_back(std::string(*name), function);
        }
    }
    // A weak sink: a binding wrapped for an earlier trace stops writing once the trace is replaced or stopped.
    const std::weak_ptr<Log> sink = m_trace;
    for (auto& [name, function] : natives) {
        m_vm->registerFunction(name, [sink, name, function](std::span<const Value> args) {
            // The arguments are shown before the call: they live on the VM's stack, which a binding that calls back
            // into the scripts reuses.
            const std::string called = std::format("{}({})", name, tracedArgs(args));
            auto results = function->native(args);
            if (const std::shared_ptr<Log> trace = sink.lock(); trace && *trace) {
                const std::string shown =
                    results && !results->empty() ? std::format(" -> {}", tracedArgs(*results)) : std::string{};
                (*trace)(std::format("{}{}\n", called, shown));
            }
            return results;
        });
    }
}

void ScriptSystem::destroy() {
    if (!m_vm) {
        return;
    }
    CONEY_ASSERT(!m_vm->running());
    m_skippedBefore += m_vm->nilCalls();
    m_vm.reset();
    m_schedule.clear();
    m_updateFunction.clear();
}

LuaVm& ScriptSystem::vm() {
    CONEY_ASSERT(m_vm != nullptr);
    return *m_vm;
}

bool ScriptSystem::runFile(std::string_view name) {
    if (!m_vm) {
        return false;
    }
    auto bytes = m_source ? m_source(name)
                          : std::expected<std::vector<std::byte>, Error>(
                                std::unexpected(Error{ErrorCode::NotFound, "no script source"}));
    if (!bytes) {
        reportError(name, bytes.error());
        return false;
    }
    // The original collects garbage after each file; Coney's VM has nothing to collect.
    return runChunk(*bytes, name);
}

std::expected<std::vector<std::byte>, Error> ScriptSystem::readFile(std::string_view name) const {
    if (!m_source) {
        return fail(ErrorCode::NotFound, "no script source");
    }
    return m_source(name);
}

void ScriptSystem::runFiles(std::span<const std::string_view> names) {
    for (const std::string_view name : names) {
        runFile(name);
    }
}

void ScriptSystem::enterLevel(std::string_view level) {
    runFile(kGlobalScript);
    runFile(std::format("{}.lua", level));
}

bool ScriptSystem::runChunk(std::span<const std::byte> bytes, std::string_view name) {
    auto chunk = loadLuaChunk(bytes);
    if (!chunk) {
        reportError(name, chunk.error());
        return false;
    }
    auto ran = m_vm->run(std::move(*chunk));
    noteSkippedCalls();
    if (!ran) {
        reportError(name, ran.error());
        return false;
    }
    return true;
}

std::pair<Value, Value> ScriptSystem::resolve(std::string_view name) const {
    // Walk the parts from the globals: every part but the last must be a table. A `:` before the last part makes its
    // table the `self` argument.
    Value current(m_vm->globalsTable());
    Value self;
    std::size_t start = 0;
    bool method = false;
    while (true) {
        const std::size_t end = name.find_first_of(".:", start);
        const std::string_view part = name.substr(start, end == std::string_view::npos ? end : end - start);
        const std::shared_ptr<Table>& table = current.table();
        if (!table) {
            return {};
        }
        Value next = table->field(part);
        if (end == std::string_view::npos) {
            return {std::move(next), method ? current : Value()};
        }
        method = name[end] == ':';
        current = std::move(next);
        start = end + 1;
    }
}

bool ScriptSystem::hasFunction(std::string_view name) const {
    return m_vm != nullptr && resolve(name).first.function() != nullptr;
}

bool ScriptSystem::call(std::string_view name, std::span<const Value> args) {
    return callResults(name, args).has_value();
}

std::optional<std::vector<Value>> ScriptSystem::callResults(std::string_view name, std::span<const Value> args) {
    if (!m_vm) {
        return std::nullopt;
    }
    const auto [function, self] = resolve(name);
    if (!function.function()) {
        log(std::format("script: {} is not a function; not called", name));
        return std::nullopt;
    }
    std::vector<Value> callArgs;
    if (!self.isNil()) {
        callArgs.push_back(self);
    }
    callArgs.insert(callArgs.end(), args.begin(), args.end());
    if (m_trace && *m_trace) {
        (*m_trace)(std::format("> {}({})\n", name, tracedArgs(args)));
    }
    auto result = m_vm->call(function, callArgs);
    noteSkippedCalls();
    if (!result) {
        reportError(name, result.error());
        return std::nullopt;
    }
    return std::move(*result);
}

void ScriptSystem::schedule(std::string name, std::uint64_t delayMs, std::span<const double> args) {
    CONEY_ASSERT(args.size() <= 2);
    ScheduledCall entry{m_nowMs + delayMs, m_nextSequence++, std::move(name), {args.begin(), args.end()}};
    // Keep the list ordered by due time, then by when the call was scheduled.
    const auto at = std::ranges::upper_bound(m_schedule, entry, [](const ScheduledCall& a, const ScheduledCall& b) {
        return a.dueMs != b.dueMs ? a.dueMs < b.dueMs : a.sequence < b.sequence;
    });
    m_schedule.insert(at, std::move(entry));
}

void ScriptSystem::flushScheduled(std::string_view name) {
    std::erase_if(m_schedule, [name](const ScheduledCall& entry) { return name.empty() || entry.function == name; });
}

void ScriptSystem::update(std::uint64_t nowMs, double stepSeconds) {
    m_nowMs = nowMs;
    if (!m_vm) {
        return;
    }
    // The calls that are due, oldest first. Only those scheduled before this update began run now; a call may
    // schedule or flush others, so the list is searched afresh each time.
    const std::uint64_t firstNew = m_nextSequence;
    while (m_vm) {
        const auto due = std::ranges::find_if(m_schedule, [nowMs, firstNew](const ScheduledCall& entry) {
            return entry.dueMs <= nowMs && entry.sequence < firstNew;
        });
        if (due == m_schedule.end()) {
            break;
        }
        const ScheduledCall entry = std::move(*due);
        m_schedule.erase(due);
        std::vector<Value> args(entry.args.begin(), entry.args.end());
        // A name that does not resolve to a function is dropped silently, as in the original.
        if (resolve(entry.function).first.function()) {
            call(entry.function, args);
        }
    }
    // Then the update function, with the step in milliseconds.
    if (m_vm && !m_updateFunction.empty()) {
        const std::array<Value, 1> step{Value(stepSeconds * 1000.0)};
        call(m_updateFunction, step);
    }
}

std::uint64_t ScriptSystem::skippedCalls() const { return m_skippedBefore + (m_vm ? m_vm->nilCalls() : 0); }

void ScriptSystem::log(std::string_view line) const {
    if (m_log) {
        m_log(std::format("{}\n", line));
    }
}

void ScriptSystem::reportError(std::string_view where, const Error& error) {
    ++m_errors;
    log(std::format("script error: {}: {}", where, error.message));
}

void ScriptSystem::noteSkippedCalls() {
    if (!m_vm) {
        return;
    }
    for (const auto& [name, count] : m_vm->nilCallsByName()) {
        if (m_skippedNames.insert(name).second) {
            log(std::format("script: `{}` is not a binding Coney has; its calls are skipped",
                            name.empty() ? "?" : name));
        }
    }
}

} // namespace coney::script
