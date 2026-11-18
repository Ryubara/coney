// SPDX-License-Identifier: GPL-3.0-or-later
// The terminal's line as the keyboard edits it: candidates while typing, Tab, Up and Down, Enter
// (docs/guides/console.md#front-end, docs/guides/studio/console.md#terminal).
#include "console/line_editor.h"

#include <array>
#include <deque>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "console/command_spec.h"
#include "console/console.h"
#include "console/hosts.h"
#include "console/registry.h"

using coney::console::ArgSpec;
using coney::console::ArgType;
using coney::console::CommandContext;
using coney::console::CommandSpec;
using coney::console::Console;
using coney::console::LineEditor;
using coney::console::Need;
using coney::console::Outcome;
using coney::console::SessionHost;
using Strings = std::vector<std::string>;

namespace {

// A session host with a fixed setting list.
class FakeSession final : public SessionHost {
  public:
    [[nodiscard]] bool has(Need /*need*/) const override { return true; }
    [[nodiscard]] std::optional<Strings> values(ArgType type, std::span<const std::string>) const override {
        if (type == ArgType::Setting) {
            return Strings{"fps_cap", "show_fps", "vsync"};
        }
        return std::nullopt;
    }
};

constexpr std::array<ArgSpec, 1> kGetArgs{ArgSpec{.name = "setting", .type = ArgType::Setting, .help = "A setting"}};

// A console with `help`, `here`, `where` and `get <setting>`, each printing its name.
struct EditorConsole {
    FakeSession session;
    Console console{{}, {.session = &session}};
    EditorConsole() {
        const auto prints = [](std::string_view name) {
            return [name](CommandContext& context) -> std::expected<void, std::string> {
                context.print(std::string(name) + " ran");
                return {};
            };
        };
        auto& registry = console.registry();
        REQUIRE(registry.addBuiltin(CommandSpec{.name = "help", .summary = "Lists."}, prints("help")));
        REQUIRE(registry.addBuiltin(CommandSpec{.name = "here", .summary = "Reads."}, prints("here")));
        REQUIRE(registry.addBuiltin(CommandSpec{.name = "where", .summary = "Reads."}, prints("where")));
        REQUIRE(registry.addBuiltin(CommandSpec{.name = "get", .summary = "Reads.", .args = kGetArgs}, prints("get")));
    }
};

} // namespace

TEST_CASE("the line lists its candidates while typing, and none when empty", "[console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    EditorConsole fixture;
    LineEditor editor(fixture.console);
    CHECK(editor.candidates().empty());
    editor.setText("he");
    CHECK(editor.candidates() == Strings{"help", "here"});
    editor.setText("get ");
    CHECK(editor.candidates() == Strings{"get fps_cap", "get show_fps", "get vsync"});
    editor.setText("");
    CHECK(editor.candidates().empty());
}

TEST_CASE("Tab takes the one candidate with a space, or what several begin with", "[console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    EditorConsole fixture;
    LineEditor editor(fixture.console);
    editor.setText("wh");
    CHECK(editor.complete());
    CHECK(editor.text() == "where ");
    // A whole command with nothing after it lists no candidates.
    CHECK(editor.candidates().empty());
    editor.setText("h");
    CHECK(editor.complete());
    CHECK(editor.text() == "he");
    // Nothing more they share: the line stays.
    CHECK_FALSE(editor.complete());
    CHECK(editor.text() == "he");
    editor.setText("get s");
    CHECK(editor.complete());
    CHECK(editor.text() == "get show_fps ");
    editor.setText("nothing");
    CHECK_FALSE(editor.complete());
}

TEST_CASE("Enter runs the line, empties it and keeps it in the history", "[console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    EditorConsole fixture;
    LineEditor editor(fixture.console);
    // The space a completion leaves is not part of the line entered.
    editor.setText("where ");
    CHECK(editor.submit() == Outcome::Ran);
    CHECK(editor.text().empty());
    CHECK(editor.candidates().empty());
    REQUIRE_FALSE(fixture.console.terminal().output().empty());
    CHECK(fixture.console.terminal().output().front().text == "> where");
    CHECK(fixture.console.terminal().output().back().text == "where ran");
    CHECK(fixture.console.terminal().history() == std::deque<std::string>{"where"});
}

TEST_CASE("Up and Down walk the lines entered before, and Down past the newest empties the line",
          "[console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    EditorConsole fixture;
    LineEditor editor(fixture.console);
    for (const char* line : {"help", "where"}) {
        editor.setText(line);
        (void)editor.submit();
    }
    editor.previous();
    CHECK(editor.text() == "where");
    editor.previous();
    CHECK(editor.text() == "help");
    editor.previous();
    CHECK(editor.text() == "help");
    editor.next();
    CHECK(editor.text() == "where");
    editor.next();
    CHECK(editor.text().empty());
}

TEST_CASE("a line typed in a test-mode run is ignored", "[console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#test-mode
    FakeSession session;
    Console console({.testMode = true}, {.session = &session});
    LineEditor editor(console);
    editor.setText("where");
    CHECK(editor.submit() == Outcome::Ignored);
    CHECK(console.terminal().output().empty());
}
