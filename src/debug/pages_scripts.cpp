// SPDX-License-Identifier: GPL-3.0-or-later
// The script pages of the debug menus: Natives, Lua console, Cheats and Levels. Coney's own tools (no @orig).
#include "debug/debug_pages.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "debug/debug_session.h"

namespace coney::debug {

namespace {

// Lines of the log a page shows.
constexpr std::size_t kLogLines = 10;

// The largest magnitude a number argument's editor allows.
constexpr double kArgumentRange = 1.0e9;

// The results as one log line: `= 1, "x"`, or `= (nothing)`.
std::string resultLine(const std::vector<script::Value>& results) {
    if (results.empty()) {
        return "= (nothing)";
    }
    std::string line = "=";
    for (std::size_t i = 0; i < results.size(); ++i) {
        line += (i == 0 ? " " : ", ") + formatValue(results[i]);
    }
    return line;
}

// Calls the binding `args` describes through the session's script state and logs the call, its results and what it
// wrote into table arguments.
void callAndLog(DebugSession& session, NativeArguments& args) {
    std::vector<script::Value> values = args.toValues();
    const std::string_view name = args.signature().name;
    session.print(std::format("> {}  [{} state]", args.callText(), session.usingGameScripts() ? "game" : "sandbox"));
    auto results = callNative(session.vm(), name, values);
    if (!results) {
        session.print("! " + results.error().message);
        return;
    }
    session.print(resultLine(*results));
    for (std::size_t i = 0; i < args.tables().size(); ++i) {
        if (const auto& table = args.tables()[i]; table) {
            session.print(std::format("  {} now {}", args.signature().args[i].name, formatValue(script::Value(table))));
        }
    }
}

// The editor item of argument `index` of `args`.
MenuItem argumentItem(const std::shared_ptr<NativeArguments>& args, std::size_t index) {
    const NativeArg& arg = args->signature().args[index];
    const std::string label = std::format("{} {} ({})", index + 1, arg.name, nativeArgTypeName(arg.type));
    auto* value = &args->values()[index];
    MenuItem item;
    switch (arg.type) {
    case NativeArgType::Number:
    case NativeArgType::Integer:
    case NativeArgType::Handle: {
        const bool whole = arg.type != NativeArgType::Number;
        item = numberItem(
            label, [args, value] { return value->number; }, [args, value](double v) { value->number = v; },
            -kArgumentRange, kArgumentRange, whole ? 1.0 : 0.1, whole);
        item.defaultValue = parseNumber(arg.defaultValue).value_or(arg.defaultValue == "true" ? 1.0 : 0.0);
        break;
    }
    case NativeArgType::Boolean:
        item = toggleItem(label, [args, value] { return value->flag; }, [args, value](bool on) { value->flag = on; });
        break;
    case NativeArgType::String:
    case NativeArgType::StringTable:
        item = textItem(
            label, [args, value] { return value->text; },
            [args, value](const std::string& text) { value->text = text; }, false);
        break;
    case NativeArgType::NumberTable:
        // Typed as numbers separated by commas; anything that is not a number reads as 0.
        item = textItem(
            label,
            [args, value] {
                std::string text;
                for (std::size_t n = 0; n < value->numbers.size(); ++n) {
                    text += std::format("{}{:.14g}", n == 0 ? "" : ",", value->numbers[n]);
                }
                return text;
            },
            [args, value](const std::string& text) {
                value->numbers.clear();
                std::string_view rest = text;
                while (!rest.empty()) {
                    const auto comma = rest.find(',');
                    value->numbers.push_back(parseNumber(rest.substr(0, comma)).value_or(0.0));
                    rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
                }
            },
            false);
        break;
    case NativeArgType::Userdata:
        item = watchItem(label, [] { return std::string("nil (a tolua object)"); });
        break;
    }
    if (!arg.defaultValue.empty()) {
        item.help = std::format("Default {} when left off.", arg.defaultValue);
    }
    return item;
}

// The page of one binding: its status, the argument editor, the call and the log.
std::shared_ptr<MenuPage> bindingPage(DebugSession& session, const NativeSignature& signature) {
    auto page = std::make_shared<MenuPage>(std::string(signature.name));
    auto args = std::make_shared<NativeArguments>(signature);
    const NativeStatus status = nativeStatus(signature.name);
    page->add(watchItem("Status", [status, &signature] {
        const std::string overloads =
            signature.overloads == 0 ? std::string{} : std::format(", {} older registration(s)", signature.overloads);
        return std::format("{} ({}){}", nativeStatusName(status), signature.category, overloads);
    }));
    page->add(watchItem("Call", [args] { return args->callText(); }));
    for (std::size_t i = 0; i < signature.args.size(); ++i) {
        page->add(argumentItem(args, i));
    }
    if (!signature.args.empty()) {
        // How many arguments to pass: fewer leaves the rest off, so their defaults apply.
        const auto count = static_cast<double>(signature.args.size());
        MenuItem passed = numberItem(
            "Arguments passed",
            [args] {
                const auto& values = args->values();
                return static_cast<double>(std::ranges::find_if(values, [](const auto& v) { return v.omitted; }) -
                                           values.begin());
            },
            [args](double n) {
                auto& values = args->values();
                for (std::size_t i = 0; i < values.size(); ++i) {
                    values[i].omitted = static_cast<double>(i) >= n;
                }
            },
            0, count, 1, true);
        passed.defaultValue = count;
        page->add(std::move(passed));
    }
    page->add(actionItem("Call it", [&session, args] { callAndLog(session, *args); }))
        .withHelp("Calls the binding through the script state, as a script's call would.");
    page->add(watchItem("Recorded calls", [&session, &signature] {
        const script::RecordedCalls* recorded = session.recorded();
        return recorded == nullptr ? std::string("-") : std::format("{}", recorded->count(signature.name));
    }));
    page->add(logItem("Log", [&session] { return session.log().last(kLogLines); }));
    return page;
}

// The page of one category: its bindings with their status.
void fillCategory(MenuPage& page, DebugSession& session, std::string_view category) {
    for (const NativeSignature& signature : nativeSignatures()) {
        if (signature.category != category) {
            continue;
        }
        MenuItem item = submenuItem(std::string(signature.name),
                                    [&session, &signature] { return bindingPage(session, signature); });
        item.detail = std::string(nativeStatusName(nativeStatus(signature.name)));
        page.add(std::move(item));
    }
}

} // namespace

void addNativesPage(DebugSession& session) {
    session.model().addPage(
        "Natives",
        [&session](MenuPage& page) {
            // The categories in the table's order, with their counts.
            std::vector<std::string_view> categories;
            for (const NativeSignature& signature : nativeSignatures()) {
                if (std::ranges::find(categories, signature.category) == categories.end()) {
                    categories.push_back(signature.category);
                }
            }
            for (const std::string_view category : categories) {
                const auto count = std::ranges::count(nativeSignatures(), category, &NativeSignature::category);
                MenuItem item = submenuItem(std::string(category), [&session, category] {
                    auto sub = std::make_shared<MenuPage>(std::string(category));
                    sub->setRebuild([&session, category](MenuPage& p) { fillCategory(p, session, category); });
                    return sub;
                });
                item.detail = std::format("{}", count);
                page.add(std::move(item));
            }
            page.add(watchItem("Script state",
                               [&session] { return std::string(session.usingGameScripts() ? "game" : "sandbox"); }))
                .withHelp("Calls go to the game's script state when a game runs, else to the menus' own sandbox.");
        },
        "Every script binding: its Coney status, an argument editor and a call.");
}

void addConsolePage(DebugSession& session) {
    session.model().addPage(
        "Lua console",
        [&session](MenuPage& page) {
            auto draft = std::make_shared<std::string>();
            MenuItem line = textItem(
                "Run line", [draft] { return *draft; },
                [&session, draft](const std::string& text) {
                    *draft = text;
                    session.print("> " + text);
                    auto printed = session.console().run(session.vm(), text);
                    if (!printed) {
                        session.print("! " + printed.error().message);
                        return;
                    }
                    for (std::string& out : *printed) {
                        session.print(std::move(out));
                    }
                },
                false);
            line.history = session.console().history();
            line.maxLength = 120;
            line.help = "A Lua statement or expression: SCENETEST = 1, GetLevelId(2), =ToInt(3.7).";
            page.add(std::move(line));
            auto file = std::make_shared<std::string>();
            MenuItem runFile = textItem(
                "Run file", [file] { return *file; },
                [&session, file](const std::string& path) {
                    *file = path;
                    auto printed = session.console().runFile(session.vm(), path);
                    if (!printed) {
                        session.print("! " + printed.error().message);
                        return;
                    }
                    for (std::string& out : *printed) {
                        session.print(std::move(out));
                    }
                },
                false);
            runFile.maxLength = 200;
            runFile.help = "A text file of console lines, or a compiled Lua 4.0 chunk.";
            page.add(std::move(runFile));
            page.add(watchItem("Script state",
                               [&session] { return std::string(session.usingGameScripts() ? "game" : "sandbox"); }));
            page.add(actionItem("Clear log", [&session] { session.log().clear(); }));
            page.add(logItem("Log", [&session] { return session.log().last(kLogLines); }));
        },
        "Runs Lua in the game's script state.");
}

void addCheatsPage(DebugSession& session) {
    session.model().addPage(
        "Cheats",
        [&session](MenuPage& page) {
            page.add(watchItem("Callback",
                               [&session] {
                                   return session.vm().global(kCheatCallback).isNil()
                                              ? std::string("not set (no level script)")
                                              : std::string(kCheatCallback);
                               }))
                .withHelp(
                    "global.lua defines it; the pad sequence checker is not in Coney yet (docs/research/debug.md).");
            for (std::size_t code = 0; code < kCheatEffects.size(); ++code) {
                page.add(actionItem(std::format("{:02} {}", code, kCheatEffects.at(code)), [&session, code] {
                    // What the checker does on a match: call the script's callback with the code's index.
                    const std::vector<script::Value> args{script::Value(static_cast<double>(code))};
                    session.print(std::format("> {}({})", kCheatCallback, code));
                    auto results = callNative(session.vm(), kCheatCallback, args);
                    session.print(results ? resultLine(*results) : "! " + results.error().message);
                }));
            }
            page.add(logItem("Log", [&session] { return session.log().last(kLogLines); }));
        },
        "The retail cheat codes, sent to the script's cheat callback.");
}

void addLevelsPage(DebugSession& session) {
    session.model().addPage(
        "Levels",
        [&session](MenuPage& page) {
            const auto load = [&session](const std::string& name) {
                const auto& loader = session.services().loadLevel;
                if (!loader) {
                    session.print(std::format("levels: cannot load {} yet (no level loader in this run)", name));
                    return;
                }
                session.print(loader(name) ? std::format("levels: loading {}", name)
                                           : std::format("levels: {} could not be loaded", name));
            };
            auto typed = std::make_shared<std::string>();
            page.add(textItem(
                         "Load by name", [typed] { return *typed; },
                         [typed, load](const std::string& name) {
                             *typed = name;
                             load(name);
                         },
                         false))
                .withHelp("A level name such as level2.");
            GameState* state = session.services().gameState ? session.services().gameState() : nullptr;
            if (state == nullptr || state->levels.count() == 0) {
                page.add(watchItem("Level table", [] { return std::string("empty: run with --disc"); }))
                    .withHelp("The table is filled by the game's config scripts (CfgLevelName).");
            } else {
                for (std::size_t i = 0; i < LevelTable::kCapacity; ++i) {
                    if (const LevelRecord* record = state->levels.at(i); record != nullptr) {
                        page.add(actionItem(std::format("{:03} {}", i, record->name),
                                            [load, name = record->name] { load(name); }));
                    }
                }
            }
            page.add(logItem("Log", [&session] { return session.log().last(4); }));
        },
        "Load any level of the level table by name.");
}

} // namespace coney::debug
