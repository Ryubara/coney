// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU timer behind the bench's `gpu.frame` measure (docs/guides/performance.md#gpu-time): a ring of timer queries,
// tested against a fake driver so it runs headless with no GPU (docs/engine/renderer.md#benchmark).
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <vector>

#include "platform/gpu_timer.h"

namespace {

using coney::platform::GpuTimer;
using coney::platform::GpuTimerApi;

// A driver that hands out query ids, remembers which are open and finished, and gives made-up times.
struct FakeDriver {
    std::uint32_t nextId = 1;
    std::vector<std::uint32_t> generated;
    std::vector<std::uint32_t> destroyed;
    std::vector<std::uint32_t> begun;
    int ends = 0;
    std::map<std::uint32_t, std::uint64_t> finished; // id -> nanoseconds, once the GPU is done with it

    GpuTimerApi api() {
        GpuTimerApi api;
        api.generate = [this](std::size_t count, std::uint32_t* ids) {
            for (std::size_t i = 0; i < count; ++i) {
                ids[i] = nextId++;
                generated.push_back(ids[i]);
            }
        };
        api.destroy = [this](std::size_t count, const std::uint32_t* ids) {
            for (std::size_t i = 0; i < count; ++i) {
                destroyed.push_back(ids[i]);
            }
        };
        api.begin = [this](std::uint32_t id) { begun.push_back(id); };
        api.end = [this] { ++ends; };
        api.ready = [this](std::uint32_t id) { return finished.contains(id); };
        api.result = [this](std::uint32_t id) { return finished.at(id); };
        return api;
    }
};

} // namespace

TEST_CASE("a driver without timer queries leaves the timer unavailable and every call harmless", "[gpu_timer]") {
    // Expected: unverified, #1208 (Coney's own debug tool: docs/guides/performance.md#gpu-time)
    GpuTimer timer{GpuTimerApi{}};
    CHECK_FALSE(timer.available());
    timer.begin();
    timer.end();
    CHECK_FALSE(timer.takeResult().has_value());
    CHECK(timer.skipped() == 0U);
}

TEST_CASE("a frame's time comes back once the GPU has finished it", "[gpu_timer]") {
    // Expected: unverified, #1208 (Coney's own debug tool: docs/guides/performance.md#gpu-time)
    FakeDriver driver;
    GpuTimer timer{driver.api()};
    REQUIRE(timer.available());
    timer.begin();
    timer.end();
    CHECK(driver.ends == 1);
    // Still on the GPU: nothing yet, and asking does not lose it.
    CHECK_FALSE(timer.takeResult().has_value());
    driver.finished[driver.begun.front()] = 2'500'000;
    const auto time = timer.takeResult();
    REQUIRE(time.has_value());
    CHECK(*time == 2'500'000U);
    CHECK_FALSE(timer.takeResult().has_value());
}

TEST_CASE("several frames in flight come back oldest first", "[gpu_timer]") {
    // Expected: unverified, #1208 (Coney's own debug tool: docs/guides/performance.md#gpu-time)
    FakeDriver driver;
    GpuTimer timer{driver.api()};
    for (int frame = 0; frame < 3; ++frame) {
        timer.begin();
        timer.end();
    }
    REQUIRE(driver.begun.size() == 3);
    // The newest finishes first in the driver's books, but the timer hands them back in the order they were made, and
    // never a later frame's time before an earlier one's has come.
    driver.finished[driver.begun[1]] = 20;
    CHECK_FALSE(timer.takeResult().has_value());
    driver.finished[driver.begun[0]] = 10;
    driver.finished[driver.begun[2]] = 30;
    CHECK(timer.takeResult() == 10U);
    CHECK(timer.takeResult() == 20U);
    CHECK(timer.takeResult() == 30U);
    CHECK_FALSE(timer.takeResult().has_value());
}

TEST_CASE("a GPU that falls behind drops frames instead of stalling", "[gpu_timer]") {
    // Expected: unverified, #1208 (Coney's own debug tool: docs/guides/performance.md#gpu-time)
    FakeDriver driver;
    GpuTimer timer{driver.api()};
    for (std::size_t frame = 0; frame < GpuTimer::kQueries; ++frame) {
        timer.begin();
        timer.end();
    }
    CHECK(driver.begun.size() == GpuTimer::kQueries);
    // Every query is still pending: the next frame is not measured, and its end() closes nothing.
    const int endsBefore = driver.ends;
    timer.begin();
    timer.end();
    CHECK(driver.begun.size() == GpuTimer::kQueries);
    CHECK(driver.ends == endsBefore);
    CHECK(timer.skipped() == 1U);
    // Once the oldest has been read, the ring has room again.
    driver.finished[driver.begun[0]] = 5;
    CHECK(timer.takeResult() == 5U);
    timer.begin();
    timer.end();
    CHECK(driver.begun.size() == GpuTimer::kQueries + 1);
    CHECK(timer.skipped() == 1U);
}

TEST_CASE("an end without a begin, or a begin twice, opens and closes nothing extra", "[gpu_timer]") {
    // Expected: unverified, #1208 (Coney's own debug tool: docs/guides/performance.md#gpu-time)
    FakeDriver driver;
    GpuTimer timer{driver.api()};
    timer.end();
    CHECK(driver.ends == 0);
    timer.begin();
    timer.begin(); // a query is already open: OpenGL allows one timer query at a time
    CHECK(driver.begun.size() == 1);
    timer.end();
    CHECK(driver.ends == 1);
}

TEST_CASE("the timer gives its queries back", "[gpu_timer]") {
    // Expected: unverified, #1208 (Coney's own debug tool: docs/guides/performance.md#gpu-time)
    FakeDriver driver;
    {
        GpuTimer timer{driver.api()};
        timer.begin();
        timer.end();
    }
    CHECK(driver.generated.size() == GpuTimer::kQueries);
    CHECK(driver.destroyed == driver.generated);
}
