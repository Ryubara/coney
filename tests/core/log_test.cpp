// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/log.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/log_filter.h"
#include "core/log_record.h"
#include "core/log_ring.h"

namespace logging = coney::logging;
using logging::Category;
using logging::Level;

namespace {

// Puts the process's logger back as a run starts, on the way into and out of a test, so tests do not see each
// other's sinks, steps or records.
struct FreshLogger {
    FreshLogger() { logging::reset(); }
    ~FreshLogger() { logging::reset(); }
    FreshLogger(const FreshLogger&) = delete;
    FreshLogger& operator=(const FreshLogger&) = delete;
    FreshLogger(FreshLogger&&) = delete;
    FreshLogger& operator=(FreshLogger&&) = delete;
};

// A record at `step` with `text`, the rest left as a record starts.
logging::Record recordAt(std::uint64_t step, std::string text = "x") {
    logging::Record record;
    record.step = step;
    record.text = std::move(text);
    return record;
}

} // namespace

TEST_CASE("a stamp gives the step, the play time it stands for, the level and the checkpoint", "[log]") {
    CHECK(logging::stamp(0, Level::Info, logging::kNoCheckpoint) == "[step 0 | 00:00.0 | info cp-]");
    // 45 steps of 1/30 s are 1.5 s.
    CHECK(logging::stamp(45, Level::Detail, 2) == "[step 45 | 00:01.5 | detail cp2]");
    // 61.1 s: a minute and the tenths, which round down (step 1834 is 61.13 s).
    CHECK(logging::stamp(1834, Level::Warn, 12) == "[step 1834 | 01:01.1 | warn cp12]");
    // Past an hour the minutes keep counting.
    CHECK(logging::playTime(30ULL * 60 * 75) == "75:00.0");
    CHECK(logging::stamp(3, Level::Error, 1) == "[step 3 | 00:00.1 | error cp1]");
}

TEST_CASE("a line is the stamp, the category and the text", "[log]") {
    const logging::Record record{.step = 90,
                                 .checkpoint = 3,
                                 .category = Category::Combat,
                                 .level = Level::Info,
                                 .privacy = logging::Privacy::Public,
                                 .text = "damage: 12"};
    CHECK(logging::formatLine(record) == "[step 90 | 00:03.0 | info cp3] combat: damage: 12");
}

TEST_CASE("categories and levels have names, and a list of categories parses", "[log]") {
    constexpr std::array<std::string_view, logging::kCategoryCount> kNames{"player", "ai",    "combat",
                                                                           "script", "world", "engine"};
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        const auto category = static_cast<Category>(i);
        CHECK(logging::categoryName(category) == kNames.at(i));
        CHECK(logging::parseCategory(kNames.at(i)) == category);
    }
    CHECK(logging::levelName(Level::Error) == "error");
    CHECK(logging::levelName(Level::Warn) == "warn");
    CHECK(logging::levelName(Level::Info) == "info");
    CHECK(logging::levelName(Level::Detail) == "detail");
    CHECK_FALSE(logging::parseCategory("AI").has_value()); // names are lowercase, as written in the log
    CHECK_FALSE(logging::parseCategory("physics").has_value());

    const auto two = logging::parseCategoryList("ai,combat");
    REQUIRE(two.has_value());
    CHECK(*two == std::vector<Category>{Category::Ai, Category::Combat});
    const auto all = logging::parseCategoryList("all");
    REQUIRE(all.has_value());
    CHECK(all->size() == logging::kCategoryCount);
    CHECK_FALSE(logging::parseCategoryList("ai,,combat").has_value());
    CHECK_FALSE(logging::parseCategoryList("").has_value());
    const auto bad = logging::parseCategoryList("ai,physics");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("physics") != std::string::npos);
}

TEST_CASE("the filter keeps info and up, and detail only for verbose categories", "[log]") {
    logging::LogFilter filter;
    for (std::size_t i = 0; i < logging::kCategoryCount; ++i) {
        const auto category = static_cast<Category>(i);
        CHECK(filter.passes(category, Level::Error));
        CHECK(filter.passes(category, Level::Warn));
        CHECK(filter.passes(category, Level::Info));
        CHECK_FALSE(filter.passes(category, Level::Detail));
    }
    filter.setVerbose(Category::Ai);
    filter.setVerbose(Category::Combat);
    CHECK(filter.passes(Category::Ai, Level::Detail));
    CHECK(filter.passes(Category::Combat, Level::Detail));
    CHECK_FALSE(filter.passes(Category::Player, Level::Detail));
    CHECK(filter.verbose(Category::Ai));
    CHECK_FALSE(filter.verbose(Category::World));
}

TEST_CASE("the ring keeps about 30 s of steps and forgets older records", "[log]") {
    logging::LogRing ring;
    CHECK(logging::LogRing::kDefaultSteps == 900); // 30 s at 30 steps a second
    for (std::uint64_t step = 0; step < 2000; ++step) {
        ring.add(recordAt(step));
    }
    REQUIRE(ring.size() == 901);
    CHECK(ring.records().front().step == 2000 - 1 - 900);
    CHECK(ring.records().back().step == 1999);
    CHECK(ring.dropped() == 2000 - 901);
}

TEST_CASE("the ring holds at most its record limit however many come in one step", "[log]") {
    logging::LogRing ring(900, 5);
    for (int i = 0; i < 12; ++i) {
        ring.add(recordAt(4, std::to_string(i)));
    }
    REQUIRE(ring.size() == 5);
    CHECK(ring.records().front().text == "7"); // the newest five, oldest first
    CHECK(ring.records().back().text == "11");
    CHECK(ring.dropped() == 7);
    ring.clear();
    CHECK(ring.size() == 0);
}

TEST_CASE("the logger stamps lines with the step and checkpoint set before them", "[log]") {
    const FreshLogger fresh;
    std::vector<std::string> lines;
    logging::setSink([&lines](const logging::Record& /*record*/, std::string_view line) { lines.emplace_back(line); });
    logging::setStep(30);
    logging::info(Category::World, "level load: level99 checkpoint 1");
    logging::setCheckpoint(1);
    logging::setStep(31);
    logging::warn(Category::Engine, "coney: no pads");
    logging::error(Category::Script, "script error: x");
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "[step 30 | 00:01.0 | info cp-] world: level load: level99 checkpoint 1");
    CHECK(lines[1] == "[step 31 | 00:01.0 | warn cp1] engine: coney: no pads");
    CHECK(lines[2] == "[step 31 | 00:01.0 | error cp1] script: script error: x");
    CHECK(logging::step() == 31);
    CHECK(logging::checkpoint() == 1);
    logging::clearCheckpoint();
    CHECK(logging::checkpoint() == logging::kNoCheckpoint);
}

TEST_CASE("text of several lines makes one record a line, without the empty ends", "[log]") {
    const FreshLogger fresh;
    std::vector<std::string> texts;
    logging::setSink(
        [&texts](const logging::Record& record, std::string_view /*line*/) { texts.push_back(record.text); });
    logging::info(Category::Engine, "first\n  second\r\n\nthird\n");
    CHECK(texts == std::vector<std::string>{"first", "  second", "third"});
    logging::info(Category::Engine, "\n");
    CHECK(texts.size() == 3);
}

TEST_CASE("detail the filter drops still goes into the ring, and a dump writes every level", "[log]") {
    const FreshLogger fresh;
    std::vector<std::string> shown;
    std::vector<std::string> dumped;
    logging::setSink([&shown](const logging::Record& /*record*/, std::string_view line) { shown.emplace_back(line); });
    logging::setDumpSink([&dumped](std::string_view line) { dumped.emplace_back(line); });
    logging::LogFilter filter;
    filter.setVerbose(Category::Ai);
    logging::setFilter(filter);
    logging::setStep(5);
    logging::detail(Category::Combat, "damage: hidden from the file");
    logging::detail(Category::Ai, "goal: shown, ai is verbose");
    logging::info(Category::Player, "pickup: took object 3");
    CHECK(shown.size() == 2);
    CHECK(logging::ring().size() == 3);

    logging::setStep(6);
    CHECK(logging::dump("bug report") == 3);
    REQUIRE(dumped.size() == 5);
    CHECK(dumped[0] ==
          "[step 6 | 00:00.2 | info cp-] engine: bug report: the last 3 events, every level, oldest first");
    CHECK(dumped[1] == "[step 5 | 00:00.1 | detail cp-] combat: damage: hidden from the file");
    CHECK(dumped[3] == "[step 5 | 00:00.1 | info cp-] player: pickup: took object 3");
    CHECK(dumped[4] == "[step 6 | 00:00.2 | info cp-] engine: bug report: end of the last 3 events");
    // The dump is not itself recorded, so a second dump writes the same events.
    CHECK(logging::ring().size() == 3);
}

TEST_CASE("a dump with no dump sink writes nothing and still counts", "[log]") {
    const FreshLogger fresh;
    logging::info(Category::Engine, "one");
    CHECK(logging::dump("bug report") == 1);
}

TEST_CASE("a failure dump writes the ring through the dump sink once", "[log]") {
    const FreshLogger fresh;
    std::vector<std::string> dumped;
    logging::setDumpSink([&dumped](std::string_view line) { dumped.emplace_back(line); });
    logging::info(Category::World, "checkpoint: start 2");
    logging::dumpOnFailure("assertion failed");
    REQUIRE(dumped.size() == 3);
    CHECK(dumped[0].find("assertion failed: the last 1 events") != std::string::npos);
    logging::dumpOnFailure("crash");
    CHECK(dumped.size() == 3); // a crash after the assert does not dump again
}

TEST_CASE("a routed line gets its category and level from its prefix", "[log]") {
    using logging::Privacy;
    const auto check = [](std::string_view line, Category category, Level level, Privacy privacy = Privacy::Public) {
        const logging::Route route = logging::routeOf(line);
        CHECK(route.category == category);
        CHECK(route.level == level);
        CHECK(route.privacy == privacy);
    };
    check("script error: level99.lua: x", Category::Script, Level::Error);
    check("script: `Foo` is not a binding Coney has", Category::Script, Level::Info);
    check("script print: anything", Category::Script, Level::Detail, Privacy::LocalOnly);
    check("pickup: took object 4", Category::Player, Level::Info);
    check("tag: player starts spraying tag 2", Category::Player, Level::Info);
    check("scout: gang called", Category::Ai, Level::Info);
    check("level flow: starting level2", Category::World, Level::Info);
    check("gameplay: level99 checkpoint 1", Category::World, Level::Info);
    check("scene: l99_c1 (id 3)", Category::World, Level::Info);
    check("audio: offline", Category::Engine, Level::Info);
    check("no prefix at all", Category::Engine, Level::Info);
}

TEST_CASE("a local-only line reaches the sink but the ring keeps only its length", "[log]") {
    const FreshLogger fresh;
    std::vector<logging::Record> seen;
    logging::setSink([&seen](const logging::Record& record, std::string_view /*line*/) { seen.push_back(record); });
    logging::writeRouted("script print: some words the scripts printed\n");
    REQUIRE(seen.size() == 0); // detail: not shown by default
    logging::LogFilter verbose;
    verbose.setVerbose(Category::Script);
    logging::setFilter(verbose);
    logging::writeRouted("script print: words\n");
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].text == "script print: words");
    CHECK(seen[0].privacy == logging::Privacy::LocalOnly);
    REQUIRE(logging::ring().size() == 2);
    CHECK(logging::ring().records().back().text == "script print: (5 characters, kept out of log files)");
    CHECK(logging::withheld(seen[0]).text == "script print: (5 characters, kept out of log files)");
}

TEST_CASE("the same calls give the same lines: nothing in a line reads a clock", "[log]") {
    const auto run = [] {
        const FreshLogger fresh;
        std::vector<std::string> lines;
        logging::setSink(
            [&lines](const logging::Record& /*record*/, std::string_view line) { lines.emplace_back(line); });
        for (std::uint64_t step = 0; step < 100; step += 7) {
            logging::setStep(step);
            logging::info(Category::Combat, "damage: 3");
        }
        return lines;
    };
    CHECK(run() == run());
}
