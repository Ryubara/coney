// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/log_output.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/log.h"
#include "support/fixtures.h"

namespace logging = coney::logging;
using coney::platform::LogOutput;
using coney::test::TempDir;

namespace {

// The lines of the text file at `path`.
std::vector<std::string> linesOf(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

} // namespace

TEST_CASE("the log file gets the stamped lines that pass the filter, the game's text withheld", "[log]") {
    logging::reset();
    const TempDir dir;
    const std::filesystem::path file = dir.path() / "coney.log";
    {
        logging::LogFilter filter;
        filter.setVerbose(logging::Category::Script);
        auto output = LogOutput::start(file, filter, false);
        REQUIRE(output.has_value());
        CHECK((*output)->file() == file);
        logging::setStep(60);
        logging::setCheckpoint(2);
        logging::info(logging::Category::World, "level load: level99 checkpoint 2");
        logging::detail(logging::Category::Combat, "damage: not verbose, ring only");
        logging::writeRouted("script print: words from the scripts\n");
    }
    const std::vector<std::string> lines = linesOf(file);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "[step 60 | 00:02.0 | info cp2] world: level load: level99 checkpoint 2");
    CHECK(lines[1] == "[step 60 | 00:02.0 | detail cp2] script: script print: (22 characters, kept out of log files)");
    logging::reset();
}

TEST_CASE("a bug report writes the ring at every level into the log file", "[log]") {
    logging::reset();
    const TempDir dir;
    const std::filesystem::path file = dir.path() / "coney.log";
    {
        auto output = LogOutput::start(file, logging::LogFilter{}, false);
        REQUIRE(output.has_value());
        logging::setStep(3);
        logging::detail(logging::Category::Combat, "damage: 5");
        const std::string said = (*output)->writeBugReport();
        CHECK(said.find("the last 1 events written to") != std::string::npos);
        CHECK(said.find("coney.log") != std::string::npos);
    }
    const std::vector<std::string> lines = linesOf(file);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].find("bug report: the last 1 events") != std::string::npos);
    CHECK(lines[1] == "[step 3 | 00:00.1 | detail cp-] combat: damage: 5");
    logging::reset();
}

TEST_CASE("a new log keeps the previous run's file beside it", "[log]") {
    logging::reset();
    const TempDir dir;
    const std::filesystem::path file = dir.path() / "coney.log";
    {
        std::ofstream old(file);
        old << "the run before\n";
    }
    {
        auto output = LogOutput::start(file, logging::LogFilter{}, true);
        REQUIRE(output.has_value());
        logging::info(logging::Category::Engine, "the new run");
    }
    const std::vector<std::string> previous = linesOf(dir.path() / "coney.prev.log");
    REQUIRE(previous.size() == 1);
    CHECK(previous[0] == "the run before");
    const std::vector<std::string> now = linesOf(file);
    REQUIRE(now.size() == 1);
    CHECK(now[0].ends_with("engine: the new run"));
    logging::reset();
}

TEST_CASE("a log file that cannot be opened is an error", "[log]") {
    logging::reset();
    const TempDir dir;
    auto output = LogOutput::start(dir.path() / "no such folder" / "coney.log", logging::LogFilter{}, false);
    REQUIRE_FALSE(output.has_value());
    CHECK(output.error().code == coney::ErrorCode::Io);
    logging::reset();
}
