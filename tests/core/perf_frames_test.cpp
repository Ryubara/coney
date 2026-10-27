// SPDX-License-Identifier: GPL-3.0-or-later
// The bench's per-frame CSV (`--bench-frames`, docs/guides/performance.md#bench), on a made-up clock and counter.
#include <algorithm>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "core/perf.h"

namespace {

// A made-up clock the test moves by hand.
std::uint64_t g_now = 0;
std::uint64_t fakeClock() { return g_now; }

// One frame in which zone `csv.outer` runs `outer` ns, holding `csv.inner` for `inner` ns.
void runFrame(std::uint64_t outer, std::uint64_t inner) {
    {
        CONEY_PERF_ZONE("csv.outer");
        g_now += outer - inner;
        {
            CONEY_PERF_ZONE("csv.inner");
            g_now += inner;
        }
    }
    coney::perf::endFrame();
}

// Starts from a clean recorder with the fake clock at 1 s, and leaves it clean.
struct Fresh {
    Fresh() {
        coney::perf::resetForTests();
        g_now = 1'000'000'000;
        coney::perf::setClock(&fakeClock);
    }
    ~Fresh() { coney::perf::resetForTests(); }
    Fresh(const Fresh&) = delete;
    Fresh& operator=(const Fresh&) = delete;
    Fresh(Fresh&&) = delete;
    Fresh& operator=(Fresh&&) = delete;
};

} // namespace

// Expected: docs/guides/performance.md#bench
TEST_CASE("The frames CSV holds one row per recorded frame with every zone and counter", "[perf]") {
    const Fresh fresh;
    std::uint64_t draws = 0;
    coney::perf::addCounter("test.draws", [&draws] { return draws; });
    coney::perf::setEnabled(true);
    coney::perf::startRecording(4);
    draws = 3;
    runFrame(4'000'000, 1'000'000);
    draws = 10;
    runFrame(6'500'000, 2'000'000);
    const std::string csv = coney::perf::framesCsv(coney::perf::recorded());
    // The header names every zone (in milliseconds) and counter, in registration order, after the frame's length.
    const std::size_t headerEnd = csv.find('\n');
    REQUIRE(headerEnd != std::string::npos);
    const std::string header = csv.substr(0, headerEnd);
    CHECK(header.starts_with("frame,frame_ms,"));
    CHECK(header.find("csv.outer_ms") != std::string::npos);
    CHECK(header.find("csv.inner_ms") != std::string::npos);
    CHECK(header.ends_with(",test.draws"));
    // Two frames: the first has no length (nothing before it was timed), the second 6.5 ms; zones and counters each.
    CHECK(std::ranges::count(csv, '\n') == 3);
    const std::string second = R"(1,6.500,)";
    CHECK(csv.find(second) != std::string::npos);
    CHECK(csv.find("0,0.000,") != std::string::npos);
    CHECK(csv.find(",4.000,1.000,") != std::string::npos);
    CHECK(csv.find(",6.500,2.000,") != std::string::npos);
    CHECK(csv.ends_with(",7\n"));
}
