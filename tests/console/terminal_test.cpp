// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/terminal.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

using coney::console::kHistoryLinesMax;
using coney::console::kOutputLinesMax;
using coney::console::LineKind;
using coney::console::Terminal;

// The limits: docs/guides/studio/console.md#terminal (2,000 output lines, 200 lines of history).

TEST_CASE("the terminal keeps the last 2000 output lines, a line of text each") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    Terminal terminal;
    terminal.print("one\ntwo", LineKind::Result, "yard-brawl");
    REQUIRE(terminal.output().size() == 2);
    CHECK(terminal.output()[1].text == "two");
    CHECK(terminal.output()[1].source == "yard-brawl");
    for (std::size_t i = 0; i < kOutputLinesMax; ++i) {
        terminal.print("line " + std::to_string(i));
    }
    CHECK(terminal.output().size() == kOutputLinesMax);
    CHECK(terminal.output().front().text == "line 0");
    terminal.clear();
    CHECK(terminal.output().empty());
}

TEST_CASE("the history skips blanks and repeats and keeps the last 200 lines") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    Terminal terminal;
    terminal.remember("help");
    terminal.remember("help");
    terminal.remember("  ");
    terminal.remember("quit");
    CHECK(terminal.history().size() == 2);
    for (std::size_t i = 0; i < kHistoryLinesMax + 5; ++i) {
        terminal.remember("line " + std::to_string(i));
    }
    CHECK(terminal.history().size() == kHistoryLinesMax);
    CHECK(terminal.history().front() == "line 5");
}

TEST_CASE("up and down walk the history from the newest line") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    Terminal terminal;
    terminal.remember("first");
    terminal.remember("second");
    CHECK(terminal.previous() == "second");
    CHECK(terminal.previous() == "first");
    CHECK_FALSE(terminal.previous().has_value());
    CHECK(terminal.next() == "second");
    CHECK_FALSE(terminal.next().has_value());
    // Entering a line ends the walk: up starts from the newest again.
    CHECK(terminal.previous() == "second");
    terminal.remember("third");
    CHECK(terminal.previous() == "third");
}

TEST_CASE("the history file holds a line each, and loading keeps the last 200") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    Terminal terminal;
    terminal.remember("help");
    terminal.remember("set vsync on");
    CHECK(terminal.historyText() == "help\nset vsync on\n");
    std::string file;
    for (std::size_t i = 0; i < kHistoryLinesMax + 1; ++i) {
        file += "line " + std::to_string(i) + "\r\n";
    }
    Terminal loaded;
    loaded.loadHistory(file);
    CHECK(loaded.history().size() == kHistoryLinesMax);
    CHECK(loaded.history().front() == "line 1");
    CHECK(loaded.history().back() == "line 200");
}
