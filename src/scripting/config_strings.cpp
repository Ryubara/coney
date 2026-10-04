// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/config_strings.h"

#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "fileio/file_stream.h"

namespace coney::script {

namespace {

// The scripts the string load runs, in order: the enumerations, then the configuration that loads the strings.
constexpr std::string_view kEnumScript = "enum_preload.lua";
constexpr std::string_view kConfigScript = "config_preload2.lua";
// The largest id a string table takes.
constexpr double kMaxStringId = 4294967295.0;

// What a binding returns: no results, or an error.
using BindingResult = std::expected<std::vector<Value>, Error>;

// A binding `name(id, text)` that stores the text as string `id` of `table`.
NativeFunction stringSetter(std::string_view name, gui::StringTable table, gui::GlobalStrings& strings) {
    return [name = std::string(name), table, &strings](std::span<const Value> args) -> BindingResult {
        const std::optional<double> id = args.empty() ? std::nullopt : args[0].number();
        const std::optional<std::string_view> text = args.size() < 2 ? std::nullopt : args[1].string();
        if (!id || *id < 0 || *id > kMaxStringId || std::floor(*id) != *id) {
            return fail(ErrorCode::Invalid, std::format("{}: the id must be a whole number from 0", name));
        }
        if (!text) {
            return fail(ErrorCode::Invalid, std::format("{}: the text must be a string", name));
        }
        strings.set(table, static_cast<std::uint32_t>(*id), std::string(*text));
        return std::vector<Value>{};
    };
}

// Reads `name` through `source` and runs it in `vm`; errors name the script.
std::expected<void, Error> runScript(LuaVm& vm, const ScriptSource& source, std::string_view name) {
    auto bytes = source(name);
    if (!bytes) {
        return std::unexpected(std::move(bytes.error()));
    }
    auto chunk = loadLuaChunk(*bytes);
    if (!chunk) {
        return std::unexpected(Error{chunk.error().code, std::format("{}: {}", name, chunk.error().message)});
    }
    if (auto ran = vm.run(std::move(*chunk)); !ran) {
        return std::unexpected(Error{ran.error().code, std::format("{}: {}", name, ran.error().message)});
    }
    return {};
}

} // namespace

void addStringBindings(LuaVm& vm, gui::GlobalStrings& strings) {
    vm.registerFunction("CfgHUDMessage", stringSetter("CfgHUDMessage", gui::StringTable::Hud, strings));
    vm.registerFunction("CfgCrimeMessage", stringSetter("CfgCrimeMessage", gui::StringTable::Crime, strings));
    vm.registerFunction("CfgTutorialMessage", stringSetter("CfgTutorialMessage", gui::StringTable::Tutorial, strings));
    vm.registerFunction("CfgWarriorCommand", stringSetter("CfgWarriorCommand", gui::StringTable::Command, strings));
    vm.registerFunction("CfgAnnounceMessage", stringSetter("CfgAnnounceMessage", gui::StringTable::Announce, strings));
}

std::expected<StringLoadReport, Error> loadGlobalStrings(const ScriptSource& source, Language language,
                                                         gui::GlobalStrings& strings) {
    LuaVmOptions options;
    options.nilCallsAreNoOps = true;
    LuaVm vm(options);
    addStringBindings(vm, strings);
    // GetLanguage gives the language field's value, which the script turns into the file's code.
    vm.registerFunction("GetLanguage", [language](std::span<const Value>) -> BindingResult {
        return std::vector<Value>{Value(static_cast<double>(language))};
    });
    vm.registerFunction("GetPlatform", [](std::span<const Value>) -> BindingResult {
        return std::vector<Value>{Value(kPlatformValue)};
    });
    // doFile("config_strings_en") runs a script by name. The script gives the name without the `.lua` the WAD entry
    // has; Coney adds it when the name has none (Coney's reading of the binding).
    vm.registerFunction("doFile", [&vm, &source](std::span<const Value> args) -> BindingResult {
        const std::optional<std::string_view> name = args.empty() ? std::nullopt : args[0].string();
        if (!name) {
            return fail(ErrorCode::Invalid, "doFile: the name must be a string");
        }
        std::string file(*name);
        if (!file.ends_with(".lua")) {
            file += ".lua";
        }
        if (auto ran = runScript(vm, source, file); !ran) {
            return std::unexpected(std::move(ran.error()));
        }
        return std::vector<Value>{};
    });

    // The enumerations first, as the original's preload does, then the configuration.
    for (const std::string_view script : {kEnumScript, kConfigScript}) {
        if (auto ran = runScript(vm, source, script); !ran) {
            return std::unexpected(std::move(ran.error()));
        }
    }
    return StringLoadReport{vm.instructions(), vm.nilCalls()};
}

ScriptSource wadScriptSource(const io::Wad& wad) {
    return [&wad](std::string_view name) -> std::expected<std::vector<std::byte>, Error> {
        auto entry = wad.lookup(name);
        if (!entry) {
            return std::unexpected(std::move(entry.error()));
        }
        auto stream = wad.openEntry(**entry);
        if (!stream) {
            return std::unexpected(std::move(stream.error()));
        }
        std::vector<std::byte> bytes((*entry)->size);
        if (auto read = stream->read(bytes); !read) {
            return std::unexpected(std::move(read.error()));
        }
        return bytes;
    };
}

} // namespace coney::script
