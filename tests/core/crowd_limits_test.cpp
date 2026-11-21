// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's crowd rules (docs/engine/crowd-rules.md): the original's numbers up to its 60 humans, Coney's own past them.
#include "core/crowd_limits.h"

#include <cstddef>

#include <catch2/catch_test_macros.hpp>

using coney::crowd::humanListCap;
using coney::crowd::kCrowdScanTokenCap;
using coney::crowd::kOrigMaxHumans;
using coney::crowd::kOrigScanTokens;
using coney::crowd::scanTokens;

TEST_CASE("up to 60 brains an update has the original's five scan tokens", "[crowd][scan]") {
    // Expected: ai-fights.md#enemy-scan, confirmed (code): AI_TakeScanToken 0x00293cd0 refuses past 5
    for (std::size_t brains = 0; brains <= kOrigMaxHumans; ++brains) {
        INFO(brains << " brains");
        CHECK(scanTokens(brains) == kOrigScanTokens);
    }
}

TEST_CASE("past 60 brains an update has five tokens per 60 brains, at most twenty", "[crowd][scan]") {
    // Expected: unverified, #1569 (Coney's own rule past the original's range: docs/engine/crowd-rules.md)
    CHECK(scanTokens(61) == 6);
    CHECK(scanTokens(120) == 10);
    CHECK(scanTokens(121) == 11);
    CHECK(scanTokens(229) == 20);
    CHECK(scanTokens(240) == kCrowdScanTokenCap);
    CHECK(scanTokens(1000) == kCrowdScanTokenCap);
    CHECK(scanTokens(100000) == kCrowdScanTokenCap);
    int last = scanTokens(0);
    for (std::size_t brains = 1; brains <= 2000; ++brains) {
        const int tokens = scanTokens(brains);
        CHECK(tokens >= last);
        last = tokens;
    }
}

TEST_CASE("a list of humans holds 60 up to 60 humans and every human past them", "[crowd]") {
    // Expected: ai-brain-types.md#help-calls, confirmed (code): Humans_FindAhead 0x002274a8 lists up to 60, one per
    // slot; past 60 humans Coney's own rule (docs/engine/crowd-rules.md#lists)
    CHECK(humanListCap(0) == kOrigMaxHumans);
    CHECK(humanListCap(59) == kOrigMaxHumans);
    CHECK(humanListCap(60) == kOrigMaxHumans);
    CHECK(humanListCap(61) == 61);
    CHECK(humanListCap(1000) == 1000);
}
