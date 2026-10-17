#include "core/log.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace coney::core;
using namespace std::chrono_literals;

TEST_CASE("log lines carry a time stamp, level, category and message") {
    ManualClock clock(5s);
    Logger logger(clock, LogLevel::Debug);
    MemorySink sink;
    logger.add_sink(sink);

    clock.advance(1'234'567'891ns);
    logger.info("window", "opened {}x{}", 640, 480);
    clock.set(5s + 1000s);
    logger.error("gpu", "lost");

    REQUIRE(sink.lines.size() == 2);
    CHECK(sink.lines[0] == "[     1.234567] INFO    window: opened 640x480");
    CHECK(sink.lines[1] == "[  1000.000000] ERROR   gpu: lost");
}

TEST_CASE("each level has its name in the line") {
    ManualClock clock;
    Logger logger(clock, LogLevel::Debug);
    MemorySink sink;
    logger.add_sink(sink);

    logger.debug("a", "d");
    logger.info("a", "i");
    logger.warning("a", "w");
    logger.error("a", "e");

    REQUIRE(sink.lines.size() == 4);
    CHECK(sink.lines[0] == "[     0.000000] DEBUG   a: d");
    CHECK(sink.lines[1] == "[     0.000000] INFO    a: i");
    CHECK(sink.lines[2] == "[     0.000000] WARNING a: w");
    CHECK(sink.lines[3] == "[     0.000000] ERROR   a: e");
}

TEST_CASE("messages below the level are dropped") {
    ManualClock clock;
    Logger logger(clock, LogLevel::Warning);
    MemorySink sink;
    logger.add_sink(sink);

    logger.debug("a", "no");
    logger.info("a", "no");
    logger.warning("a", "yes");
    logger.error("a", "yes");
    REQUIRE(sink.lines.size() == 2);
    CHECK(sink.lines[0].ends_with("WARNING a: yes"));

    logger.set_level(LogLevel::Off);
    logger.error("a", "no");
    CHECK(sink.lines.size() == 2);

    logger.set_level(LogLevel::Debug);
    logger.debug("a", "yes");
    CHECK(sink.lines.size() == 3);
}

TEST_CASE("a message stays on one line") {
    ManualClock clock;
    Logger logger(clock);
    MemorySink sink;
    logger.add_sink(sink);
    logger.info("a", "first\nsecond\r\n");
    REQUIRE(sink.lines.size() == 1);
    CHECK(sink.lines[0] == "[     0.000000] INFO    a: first second");
}

TEST_CASE("every sink gets every line") {
    ManualClock clock;
    Logger logger(clock);
    MemorySink first;
    MemorySink second;
    logger.add_sink(first);
    logger.add_sink(second);
    logger.info("a", "x");
    CHECK(first.lines == second.lines);
    CHECK(first.lines.size() == 1);
}

TEST_CASE("level names parse") {
    CHECK(parse_level("debug") == LogLevel::Debug);
    CHECK(parse_level("info") == LogLevel::Info);
    CHECK(parse_level("warning") == LogLevel::Warning);
    CHECK(parse_level("warn") == LogLevel::Warning);
    CHECK(parse_level("error") == LogLevel::Error);
    CHECK(parse_level("off") == LogLevel::Off);
    CHECK_FALSE(parse_level("loud").has_value());
}
