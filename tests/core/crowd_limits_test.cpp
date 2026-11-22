// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's crowd rules (docs/engine/crowd-rules.md): the original's numbers up to its 60 humans, Coney's own past them.
#include "core/crowd_limits.h"

#include <cstddef>

#include <catch2/catch_test_macros.hpp>

#include "core/engine_limits.h"

using coney::crowd::humanListCap;
using coney::crowd::kCrowdScanTokenCap;
using coney::crowd::kOrigMaxHumans;
using coney::crowd::kOrigScanTokens;
using coney::crowd::scanTokens;

namespace {
// Coney's own rule and the engine limits preset (core/engine_limits.h).
constexpr coney::EngineLimits kConey = coney::EngineLimits::Coney;
constexpr coney::EngineLimits kOriginal = coney::EngineLimits::Original;
} // namespace

TEST_CASE("up to 60 brains an update has the original's five scan tokens", "[crowd][scan]") {
    // Expected: ai-fights.md#enemy-scan, confirmed (code): AI_TakeScanToken 0x00293cd0 refuses past 5
    for (std::size_t brains = 0; brains <= kOrigMaxHumans; ++brains) {
        INFO(brains << " brains");
        CHECK(scanTokens(brains, kConey) == kOrigScanTokens);
    }
}

TEST_CASE("past 60 brains an update has five tokens per 60 brains, at most twenty", "[crowd][scan]") {
    // Expected: unverified, #1569 (Coney's own rule past the original's range: docs/engine/crowd-rules.md)
    CHECK(scanTokens(61, kConey) == 6);
    CHECK(scanTokens(120, kConey) == 10);
    CHECK(scanTokens(121, kConey) == 11);
    CHECK(scanTokens(229, kConey) == 20);
    CHECK(scanTokens(240, kConey) == kCrowdScanTokenCap);
    CHECK(scanTokens(1000, kConey) == kCrowdScanTokenCap);
    CHECK(scanTokens(100000, kConey) == kCrowdScanTokenCap);
    int last = scanTokens(0, kConey);
    for (std::size_t brains = 1; brains <= 2000; ++brains) {
        const int tokens = scanTokens(brains, kConey);
        CHECK(tokens >= last);
        last = tokens;
    }
}

TEST_CASE("a list of humans holds 60 up to 60 humans and every human past them", "[crowd]") {
    // Expected: ai-brain-types.md#help-calls, confirmed (code): Humans_FindAhead 0x002274a8 lists up to 60, one per
    // slot; past 60 humans Coney's own rule (docs/engine/crowd-rules.md#lists)
    CHECK(humanListCap(0, kConey) == kOrigMaxHumans);
    CHECK(humanListCap(59, kConey) == kOrigMaxHumans);
    CHECK(humanListCap(60, kConey) == kOrigMaxHumans);
    CHECK(humanListCap(61, kConey) == 61);
    CHECK(humanListCap(1000, kConey) == 1000);
}

TEST_CASE("under the engine limits preset every crowd rule gives the original's number however many humans",
          "[crowd][engine-limits]") {
    // Expected: ai-fights.md#enemy-scan, confirmed (code): AI_TakeScanToken 0x00293cd0 refuses past 5; and
    // ai-brain-types.md#help-calls, confirmed (code): Humans_FindAhead 0x002274a8 lists up to 60. The preset pins each
    // lift at that number (docs/guides/running-options.md#engine-limits).
    for (std::size_t humans = 0; humans <= 2000; ++humans) {
        INFO(humans << " humans");
        CHECK(scanTokens(humans, kOriginal) == kOrigScanTokens);
        CHECK(humanListCap(humans, kOriginal) == kOrigMaxHumans);
    }
}
