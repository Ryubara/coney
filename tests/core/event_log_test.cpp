// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/event_log.h"

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace events = coney::events;

TEST_CASE("the event log stamps each event with the step set before it", "[event_log]") {
    std::vector<std::string> lines;
    events::setSink([&lines](std::uint64_t step, std::string_view kind, std::string_view name,
                             std::string_view detail) { lines.push_back(events::csvLine(step, kind, name, detail)); });
    CHECK(events::enabled());
    events::setStep(7);
    events::emit("call", "SetCheckPoint", "2");
    events::setStep(9);
    events::emit("hint", "Press, then \"go\"");
    events::setSink({});
    events::emit("sound", "0x00000001"); // no log open: dropped
    CHECK_FALSE(events::enabled());
    events::setStep(0);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "7,call,SetCheckPoint,2\n");
    CHECK(lines[1] == "9,hint,\"Press, then \"\"go\"\"\",\n");
}

TEST_CASE("a CSV field with a line break is quoted", "[event_log]") {
    CHECK(events::csvLine(1, "hint", "a\nb", "") == "1,hint,\"a\nb\",\n");
    CHECK(events::csvLine(1, "call", "F", "1, 2") == "1,call,F,\"1, 2\"\n");
}
