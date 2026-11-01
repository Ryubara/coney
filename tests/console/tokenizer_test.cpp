// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/tokenizer.h"

#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/error.h"

using coney::ErrorCode;
using coney::console::kMaxLineBytes;
using coney::console::quoteWord;
using coney::console::splitForCompletion;
using coney::console::tokenize;
using Words = std::vector<std::string>;

// The rules: docs/guides/studio/console.md#terminal (a line is a name and its arguments, quotes, `\"`, 512 bytes).

TEST_CASE("tokenize splits a line into its name and arguments at spaces and tabs") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    CHECK(tokenize("mission 3 2").value() == Words{"mission", "3", "2"});
    CHECK(tokenize("  set\tfps_cap   60  ").value() == Words{"set", "fps_cap", "60"});
}

TEST_CASE("tokenize gives no words for an empty or blank line") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    CHECK(tokenize("").value().empty());
    CHECK(tokenize("   \t ").value().empty());
}

TEST_CASE("tokenize keeps a quoted argument's spaces, and \\\" is a quote inside one") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    CHECK(tokenize(R"(log --find "level load")").value() == Words{"log", "--find", "level load"});
    // A line holding \" is an ordinary literal kept outside the macro: MSVC's preprocessor misreads a raw string
    // with \" in a macro's arguments.
    constexpr std::string_view escaped = "say \"a \\\"quoted\\\" word\"";
    CHECK(tokenize(escaped).value() == Words{"say", "a \"quoted\" word"});
    CHECK(tokenize(R"(say "")").value() == Words{"say", ""});
    // Quotes join with the text beside them into one word, as a shell's do.
    CHECK(tokenize(R"(a"b c"d)").value() == Words{"ab cd"});
    // A backslash before anything but a quote is itself. Ordinary literals again, with the backslash written \\.
    constexpr std::string_view windowsPath = "path \"C:\\dir\"";
    CHECK(tokenize(windowsPath).value() == Words{"path", "C:\\dir"});
}

TEST_CASE("tokenize takes a semicolon as text: one command per line") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    CHECK(tokenize("help; quit").value() == Words{"help;", "quit"});
    CHECK(tokenize("set a;b").value() == Words{"set", "a;b"});
}

TEST_CASE("tokenize refuses a quote left open") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    auto words = tokenize(R"(log "level load)");
    REQUIRE_FALSE(words.has_value());
    CHECK(words.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(words.error().message, Catch::Matchers::ContainsSubstring("quote"));
}

TEST_CASE("tokenize takes a line of 512 bytes and refuses a longer one, never cutting it") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    const std::string longest = "x" + std::string(kMaxLineBytes - 1, 'y');
    CHECK(tokenize(longest).value() == Words{longest});
    auto refused = tokenize(longest + "z");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(refused.error().message, Catch::Matchers::ContainsSubstring("512"));
}

TEST_CASE("splitForCompletion says when a new word starts and when a quote is open") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    const auto typing = splitForCompletion("set fps");
    CHECK(typing.words == Words{"set", "fps"});
    CHECK_FALSE(typing.trailingSpace);
    const auto next = splitForCompletion("set ");
    CHECK(next.words == Words{"set"});
    CHECK(next.trailingSpace);
    const auto open = splitForCompletion(R"(log "level lo)");
    CHECK(open.words == Words{"log", "level lo"});
    CHECK(open.openQuote);
    CHECK_FALSE(open.trailingSpace);
}

TEST_CASE("quoteWord quotes only what needs it, and tokenize reads it back") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    CHECK(quoteWord("fps_cap") == "fps_cap");
    CHECK(quoteWord("level load") == R"("level load")");
    CHECK(quoteWord("") == R"("")");
    constexpr std::string_view quoted = "\"say \\\"hi\\\"\"";
    CHECK(quoteWord("say \"hi\"") == quoted);
    CHECK(tokenize(quoteWord(R"(say "hi" now)")).value() == Words{R"(say "hi" now)"});
}
