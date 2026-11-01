// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/console.h"

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "console/command_spec.h"
#include "console/hosts.h"
#include "console/registry.h"
#include "console/terminal.h"
#include "core/log.h"

using Catch::Matchers::ContainsSubstring;
using coney::console::Access;
using coney::console::ArgSpec;
using coney::console::ArgType;
using coney::console::CommandContext;
using coney::console::CommandSpec;
using coney::console::Console;
using coney::console::ConsoleHosts;
using coney::console::ConsoleSettings;
using coney::console::HistoryStore;
using coney::console::LineKind;
using coney::console::LineSource;
using coney::console::Need;
using coney::console::needBit;
using coney::console::NetworkHost;
using coney::console::Outcome;
using coney::console::Scope;
using coney::console::SessionHost;
using coney::console::SessionRole;
using Strings = std::vector<std::string>;

// The rules: docs/guides/studio/console.md#terminal (when a command runs), #access (scope), #test-mode.

namespace {

// A network host whose role the test sets.
class FakeNetwork final : public NetworkHost {
  public:
    SessionRole current = SessionRole::Offline;
    [[nodiscard]] SessionRole role() const override { return current; }
};

// A session host with a level in play and a fixed setting list, no profile.
class FakeSession final : public SessionHost {
  public:
    [[nodiscard]] bool has(Need need) const override { return need == Need::Game || need == Need::Level; }
    [[nodiscard]] std::optional<Strings> values(ArgType type, std::span<const std::string>) const override {
        if (type == ArgType::Setting) {
            return Strings{"fps_cap", "show_fps", "vsync"};
        }
        return std::nullopt;
    }
};

// A history store that counts what is asked of it.
class FakeHistory final : public HistoryStore {
  public:
    std::optional<std::string> kept;
    int loads = 0;
    int saves = 0;
    [[nodiscard]] std::optional<std::string> load() override {
        ++loads;
        return kept;
    }
    void save(std::string_view text) override {
        ++saves;
        kept = std::string(text);
    }
};

constexpr std::array<ArgSpec, 2> kSetArgs{
    ArgSpec{.name = "setting", .type = ArgType::Setting, .help = "A setting's name"},
    ArgSpec{.name = "value", .type = ArgType::Int, .help = "Its new value"},
};
constexpr std::array<ArgSpec, 1> kWaveArgs{
    ArgSpec{.name = "count", .type = ArgType::Int, .help = "How many", .optional = true, .defaultValue = "1"},
};

// What the test commands did, in order.
struct Trace {
    Strings ran;
};

// A console with test commands: `where` (local, reads), `set` (local, changes), `wave` (session) and `profile-only`
// (local, needs a profile). Each appends its name and arguments to `trace` and prints a line.
void addCommands(Console& console, Trace& trace) {
    const auto recorder = [&trace](std::string_view name) {
        return [&trace, name](CommandContext& context) -> std::expected<void, std::string> {
            std::string entry(name);
            for (const std::string& value : context.args.values()) {
                entry += " " + value;
            }
            entry += " slot" + std::to_string(context.slot);
            trace.ran.push_back(entry);
            context.print(std::string(name) + " done");
            return {};
        };
    };
    auto& registry = console.registry();
    REQUIRE(registry.addBuiltin(CommandSpec{.name = "where", .group = "T", .summary = "Reads."}, recorder("where")));
    REQUIRE(registry.addBuiltin(
        CommandSpec{.name = "set", .group = "T", .changes = true, .summary = "Changes.", .args = kSetArgs},
        recorder("set")));
    REQUIRE(registry.addBuiltin(
        CommandSpec{.name = "wave", .group = "T", .scope = Scope::Session, .summary = "Session.", .args = kWaveArgs},
        recorder("wave")));
    REQUIRE(registry.addBuiltin(
        CommandSpec{.name = "profile-only", .group = "T", .summary = "Needs.", .needs = needBit(Need::Profile)},
        recorder("profile-only")));
}

// The text of the terminal's last line.
std::string lastLine(const Console& console) {
    REQUIRE_FALSE(console.terminal().output().empty());
    return console.terminal().output().back().text;
}

} // namespace

TEST_CASE("a local command that only reads runs at once") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    FakeSession session;
    Console console({}, {.session = &session});
    Trace trace;
    addCommands(console, trace);
    CHECK(console.submit("where", LineSource::Typed) == Outcome::Ran);
    CHECK(trace.ran == Strings{"where slot0"});
    const auto& output = console.terminal().output();
    REQUIRE(output.size() == 2);
    CHECK(output[0].kind == LineKind::Echo);
    CHECK(output[0].text == "> where");
    CHECK(output[1].text == "where done");
}

TEST_CASE("session commands and local ones that change something wait for the step boundary, in typed order") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    FakeSession session;
    Console console({}, {.session = &session});
    Trace trace;
    addCommands(console, trace);
    CHECK(console.submit("wave 3", LineSource::Typed) == Outcome::Queued);
    CHECK(console.submit("set fps_cap 60", LineSource::Typed) == Outcome::Queued);
    CHECK(console.submit("wave", LineSource::Typed, 1) == Outcome::Queued);
    // Nothing ran between the steps' boundaries; a reading still runs at once.
    CHECK(trace.ran.empty());
    CHECK(console.submit("where", LineSource::Typed) == Outcome::Ran);
    CHECK(console.queued() == 3);
    CHECK(console.runQueued() == 3);
    CHECK(trace.ran == Strings{"where slot0", "wave 3 slot0", "set fps_cap 60 slot0", "wave 1 slot1"});
    CHECK(console.queued() == 0);
    CHECK(console.runQueued() == 0);
}

TEST_CASE("a line is checked when entered, so a bad one is answered before any step") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    FakeSession session;
    Console console({}, {.session = &session});
    Trace trace;
    addCommands(console, trace);
    CHECK(console.submit("wave two", LineSource::Typed) == Outcome::Refused);
    CHECK_THAT(lastLine(console), ContainsSubstring(R"(expected a whole number, got "two")"));
    CHECK(console.submit("set vsync", LineSource::Typed) == Outcome::Refused);
    CHECK_THAT(lastLine(console), ContainsSubstring("usage: set <setting> <value>"));
    CHECK(console.submit("set fov 1", LineSource::Typed) == Outcome::Refused);
    CHECK(console.submit("where now", LineSource::Typed) == Outcome::Refused);
    CHECK_THAT(lastLine(console), ContainsSubstring("too many arguments"));
    CHECK(console.submit("wher", LineSource::Typed) == Outcome::Refused);
    CHECK(lastLine(console) == R"(unknown command "wher"; did you mean "where"? help lists the commands)");
    CHECK(console.submit(R"(where ")", LineSource::Typed) == Outcome::Refused);
    CHECK(console.terminal().output().back().kind == LineKind::Error);
    CHECK(console.queued() == 0);
    CHECK(trace.ran.empty());
}

TEST_CASE("a command whose need is not met is refused") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    FakeSession session;
    Console console({}, {.session = &session});
    Trace trace;
    addCommands(console, trace);
    CHECK(console.submit("profile-only", LineSource::Typed) == Outcome::Refused);
    CHECK(lastLine(console) == R"("profile-only" needs an active profile)");
    Console bare({}, {});
    Trace bareTrace;
    addCommands(bare, bareTrace);
    CHECK(bare.submit("profile-only", LineSource::Typed) == Outcome::Refused);
}

TEST_CASE("a guest's session command is refused on the guest, and its local commands run") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#access
    FakeSession session;
    FakeNetwork network;
    network.current = SessionRole::Guest;
    Console console({}, {.session = &session, .network = &network});
    Trace trace;
    addCommands(console, trace);
    CHECK(console.submit("wave 2", LineSource::Typed) == Outcome::Refused);
    CHECK(lastLine(console) == "only the host can do this in a network game");
    CHECK(console.queued() == 0);
    CHECK(console.submit("where", LineSource::Typed) == Outcome::Ran);
    CHECK(console.submit("set vsync 1", LineSource::Typed) == Outcome::Queued);
    // The host may.
    network.current = SessionRole::Host;
    CHECK(console.submit("wave 2", LineSource::Typed) == Outcome::Queued);
}

TEST_CASE("a queued command whose registration went away does not run") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    Console console({}, {});
    Trace trace;
    const auto id = console.registry().addModCommand(
        "yard-brawl", CommandSpec{.name = "wave", .scope = Scope::Session, .summary = "Session."},
        [&trace](CommandContext&) -> std::expected<void, std::string> {
            trace.ran.emplace_back("wave");
            return {};
        });
    REQUIRE(id.has_value());
    CHECK(console.submit("wave", LineSource::Typed) == Outcome::Queued);
    console.registry().removeMod("yard-brawl");
    CHECK(console.runQueued() == 0);
    CHECK(trace.ran.empty());
    CHECK_THAT(lastLine(console), ContainsSubstring("no longer registered"));
}

TEST_CASE("a queued command whose registration changed its arguments does not run on the old values") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#mod-commands (a mod's restart between two steps)
    Console console({}, {});
    int ran = 0;
    const auto handler = [&ran](CommandContext&) -> std::expected<void, std::string> {
        ++ran;
        return {};
    };
    REQUIRE(console.registry().addModCommand(
        "yard-brawl", CommandSpec{.name = "wave", .scope = Scope::Session, .summary = "Waves.", .args = kSetArgs},
        handler));
    CHECK(console.submit("wave vsync 1", LineSource::Typed) == Outcome::Queued);
    // The mod restarts with another argument list before the step boundary.
    REQUIRE(console.registry().addModCommand(
        "yard-brawl", CommandSpec{.name = "wave", .scope = Scope::Session, .summary = "Waves."}, handler));
    CHECK(console.runQueued() == 0);
    CHECK(ran == 0);
    CHECK_THAT(lastLine(console), ContainsSubstring("changed"));
}

TEST_CASE("a mod command's output carries the mod's name, and a handler's error is shown") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#mod-commands
    Console console({}, {});
    REQUIRE(console.registry().addModCommand("yard-brawl", CommandSpec{.name = "hello", .summary = "Greets."},
                                             [](CommandContext& context) -> std::expected<void, std::string> {
                                                 context.print("hi");
                                                 return {};
                                             }));
    REQUIRE(console.registry().addBuiltin(CommandSpec{.name = "fails", .summary = "Fails."},
                                          [](CommandContext&) -> std::expected<void, std::string> {
                                              return std::unexpected(std::string("it went wrong"));
                                          }));
    CHECK(console.submit("hello", LineSource::Typed) == Outcome::Ran);
    CHECK(console.terminal().output().back().source == "yard-brawl");
    CHECK(console.submit("fails", LineSource::Typed) == Outcome::Ran);
    CHECK(console.terminal().output().back().kind == LineKind::Error);
    CHECK(lastLine(console) == "fails: it went wrong");
}

TEST_CASE("typed lines go into the history and are saved; script lines are not") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    FakeHistory history;
    history.kept = "older\n";
    Console console({}, {.history = &history});
    Trace trace;
    addCommands(console, trace);
    CHECK(history.loads == 1);
    CHECK(console.terminal().history().size() == 1);
    (void)console.submit("where", LineSource::Typed);
    (void)console.submit("nonsense", LineSource::Typed);
    (void)console.submit("where", LineSource::Script);
    CHECK(history.kept == "older\nwhere\nnonsense\n");
    CHECK(history.saves == 2);
}

TEST_CASE("in test mode the console reads and writes no history and takes lines only from the input script") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#test-mode
    FakeHistory history;
    history.kept = "left by an earlier run\n";
    Console console({.testMode = true}, {.history = &history});
    Trace trace;
    addCommands(console, trace);
    CHECK(console.terminal().history().empty());
    CHECK(console.submit("where", LineSource::Typed) == Outcome::Ignored);
    CHECK(console.submit("where", LineSource::Script) == Outcome::Ran);
    CHECK(console.submit("wave", LineSource::Script) == Outcome::Queued);
    CHECK(console.runQueued() == 1);
    CHECK(trace.ran == Strings{"where slot0", "wave 1 slot0"});
    CHECK(history.loads == 0);
    CHECK(history.saves == 0);
    CHECK(history.kept == "left by an earlier run\n");
    CHECK(console.terminal().history().empty());
}

TEST_CASE("each line and its output go to the run log, the line stamped with its step") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#determinism
    coney::logging::reset();
    Console console({}, {});
    Trace trace;
    addCommands(console, trace);
    coney::logging::setStep(42);
    (void)console.submit("where", LineSource::Script);
    (void)console.submit("wher", LineSource::Script);
    Strings lines;
    for (const auto& record : coney::logging::ring().records()) {
        lines.push_back(record.text);
    }
    CHECK(lines == Strings{"console: where", "console:   where done", "console: wher",
                           R"(console:   unknown command "wher"; did you mean "where"? help lists the commands)"});
    CHECK(coney::logging::ring().records().front().step == 42);
    coney::logging::reset();
}

TEST_CASE("completion offers command names, then each argument's values") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    FakeSession session;
    Console console({}, {.session = &session});
    Trace trace;
    addCommands(console, trace);
    CHECK(console.complete("w") == Strings{"wave", "where"});
    CHECK(console.complete("wh") == Strings{"where"});
    CHECK(console.complete("set s") == Strings{"set show_fps"});
    CHECK(console.complete("set ") == Strings{"set fps_cap", "set show_fps", "set vsync"});
    CHECK(console.complete("set vsync ").empty());
    CHECK(console.complete("nothing ").empty());
}

TEST_CASE("a blank line is ignored") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    Console console({}, {});
    CHECK(console.submit("   ", LineSource::Typed) == Outcome::Ignored);
    CHECK(console.terminal().output().empty());
}
