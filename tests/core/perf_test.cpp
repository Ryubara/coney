// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/perf.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

// A made-up clock the tests move by hand.
std::uint64_t g_now = 0;
std::uint64_t fakeClock() { return g_now; }

// The index of `name` among the zones, or the count when it is not there.
std::size_t zoneIndex(const std::string& name) {
    const std::vector<std::string> names = coney::perf::zoneNames();
    return static_cast<std::size_t>(std::ranges::find(names, name) - names.begin());
}

// One frame in which zone `test.outer` runs `outer` ns, holding `test.inner` for `inner` ns.
void runFrame(std::uint64_t outer, std::uint64_t inner) {
    {
        CONEY_PERF_ZONE("test.outer");
        g_now += outer - inner;
        {
            CONEY_PERF_ZONE("test.inner");
            g_now += inner;
        }
    }
    coney::perf::endFrame();
}

// Starts each test from a clean recorder with the fake clock at 1 s.
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

TEST_CASE("Zones record nothing while measuring is off", "[perf]") {
    const Fresh fresh;
    coney::perf::startRecording(4);
    runFrame(5'000'000, 1'000'000);
    CHECK(coney::perf::recorded().empty());
}

TEST_CASE("Nested zones each record their own time, and a frame its length", "[perf]") {
    const Fresh fresh;
    coney::perf::setEnabled(true);
    coney::perf::startRecording(4);
    runFrame(5'000'000, 1'000'000);
    runFrame(8'000'000, 2'000'000);
    const std::vector<coney::perf::FrameRecord> records = coney::perf::recorded();
    REQUIRE(records.size() == 2);
    const std::size_t outer = zoneIndex("test.outer");
    const std::size_t inner = zoneIndex("test.inner");
    REQUIRE(outer < records[1].zones.size());
    REQUIRE(inner < records[1].zones.size());
    CHECK(records[0].zones[outer] == 5'000'000);
    CHECK(records[0].zones[inner] == 1'000'000);
    CHECK(records[1].zones[outer] == 8'000'000);
    CHECK(records[1].zones[inner] == 2'000'000);
    // The first frame after measuring starts has nothing to be measured from; the second spans the first's end.
    CHECK(records[0].frameNanoseconds == 0);
    CHECK(records[1].frameNanoseconds == 8'000'000);
}

TEST_CASE("Two sites with one name add up", "[perf]") {
    const Fresh fresh;
    coney::perf::setEnabled(true);
    coney::perf::startRecording(2);
    {
        CONEY_PERF_ZONE("test.shared");
        g_now += 3;
    }
    {
        CONEY_PERF_ZONE("test.shared");
        g_now += 4;
    }
    coney::perf::endFrame();
    const std::vector<coney::perf::FrameRecord> records = coney::perf::recorded();
    REQUIRE(records.size() == 1);
    const std::size_t shared = zoneIndex("test.shared");
    REQUIRE(shared < records[0].zones.size());
    CHECK(records[0].zones[shared] == 7);
}

TEST_CASE("A counter records how much its total grew each frame", "[perf]") {
    const Fresh fresh;
    std::uint64_t draws = 100; // counted before measuring started: not in any frame
    coney::perf::addCounter("test.draws", [&draws] { return draws; });
    coney::perf::setEnabled(true);
    coney::perf::startRecording(2);
    draws += 7;
    coney::perf::endFrame();
    draws += 3;
    coney::perf::endFrame();
    const std::vector<coney::perf::FrameRecord> records = coney::perf::recorded();
    REQUIRE(records.size() == 2);
    REQUIRE(records[0].counters.size() == 1);
    CHECK(records[0].counters[0] == 7);
    CHECK(records[1].counters[0] == 3);
}

TEST_CASE("The spread of values is their mean and nearest-rank percentiles", "[perf]") {
    std::vector<double> values;
    for (int i = 100; i >= 1; --i) {
        values.push_back(static_cast<double>(i));
    }
    const coney::perf::Spread spread = coney::perf::spreadOf(values);
    CHECK(spread.mean == Catch::Approx(50.5));
    CHECK(spread.p50 == 50.0);
    CHECK(spread.p95 == 95.0);
    CHECK(spread.p99 == 99.0);
    CHECK(spread.max == 100.0);
    CHECK(coney::perf::spreadOf({}).max == 0.0);
}

TEST_CASE("The summary leaves the untimed first frame out of the frame time", "[perf]") {
    const Fresh fresh;
    coney::perf::setEnabled(true);
    coney::perf::startRecording(4);
    runFrame(4'000'000, 0);
    runFrame(6'000'000, 0);
    runFrame(10'000'000, 0);
    const coney::perf::Summary summary = coney::perf::summarise();
    CHECK(summary.frames == 3);
    CHECK(summary.frameMilliseconds.mean == Catch::Approx(8.0));
    CHECK(summary.frameMilliseconds.max == Catch::Approx(10.0));
    const auto outer = std::ranges::find_if(summary.zoneMilliseconds,
                                            [](const auto& entry) { return entry.first == "test.outer"; });
    REQUIRE(outer != summary.zoneMilliseconds.end());
    CHECK(outer->second.mean == Catch::Approx(20.0 / 3.0));
}

TEST_CASE("The recent means cover the last frames only", "[perf]") {
    const Fresh fresh;
    coney::perf::setEnabled(true);
    for (std::size_t i = 0; i < coney::perf::kRecentFrames; ++i) {
        runFrame(1'000'000, 0);
    }
    for (std::size_t i = 0; i < coney::perf::kRecentFrames; ++i) {
        runFrame(3'000'000, 0);
    }
    const coney::perf::RecentMeans means = coney::perf::recentMeans();
    CHECK(means.frames == coney::perf::kRecentFrames);
    CHECK(means.frameMilliseconds == Catch::Approx(3.0));
    const std::size_t outer = zoneIndex("test.outer");
    REQUIRE(outer < means.zoneMilliseconds.size());
    CHECK(means.zoneMilliseconds[outer] == Catch::Approx(3.0));
}

TEST_CASE("The JSON report holds the metadata, the frame time, zones and counters", "[perf]") {
    coney::perf::Summary summary;
    summary.frames = 2;
    summary.frameMilliseconds = coney::perf::Spread{.mean = 1.5, .p50 = 1.0, .p95 = 2.0, .p99 = 2.0, .max = 2.0};
    summary.zoneMilliseconds.emplace_back("render", coney::perf::Spread{.mean = 0.25});
    summary.counters.emplace_back("gl.draws", coney::perf::Spread{.mean = 12.0});
    const std::string json = coney::perf::toJson(summary, {{"fighters", "100"}, {"level", "level\"99"}});
    // The expected pieces outside the checks: MSVC's preprocessor mishandles a raw string in a macro's arguments.
    const std::string meta = R"("meta": {"fighters": "100", "level": "level\"99"})";
    const std::string frames = R"("frames": 2)";
    const std::string frameMs = R"("frame_ms": {"mean": 1.500, "p50": 1.000, "p95": 2.000, "p99": 2.000, "max": 2.000})";
    const std::string render = R"("render": {"mean": 0.250)";
    const std::string draws = R"("gl.draws": {"mean": 12.000)";
    CHECK(json.find(meta) != std::string::npos);
    CHECK(json.find(frames) != std::string::npos);
    CHECK(json.find(frameMs) != std::string::npos);
    CHECK(json.find(render) != std::string::npos);
    CHECK(json.find(draws) != std::string::npos);
}
