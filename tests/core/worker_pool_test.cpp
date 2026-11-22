// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's worker threads (docs/engine/render-threads.md#workers). Coney's own: the original runs on one EE thread
// and has no counterpart.
#include "core/worker_pool.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

using coney::WorkerPool;

namespace {

// Each index's result of a loop of `count` on a pool of `threads`: a value worked out from the index alone, written
// to the index's own slot, and how many times the slot was written.
struct LoopResult {
    std::vector<std::uint64_t> values;
    std::vector<int> runs;
};

LoopResult runLoop(std::size_t threads, std::size_t count) {
    WorkerPool pool(threads);
    LoopResult result{.values = std::vector<std::uint64_t>(count), .runs = std::vector<int>(count)};
    pool.parallelFor(count, [&result](std::size_t index) {
        std::uint64_t value = index;
        for (int round = 0; round < 64; ++round) {
            value = (value * 6364136223846793005ULL) + 1442695040888963407ULL;
        }
        result.values[index] = value;
        ++result.runs[index];
    });
    return result;
}

// How a run of the nested-loop helper ended.
struct ChildRun {
    bool started = false; // the process started
    int exitCode = -1;    // 0 when it ended by itself; a signal or a failure code when it was aborted
    std::string output;   // what it printed, its errors included
};

// Runs tests/support/worker_pool_nested_check (built beside this test binary, with `.exe` on Windows) with `argument`
// and waits for it to end: a broken invariant ends a process, so the loop that must trip one runs apart.
ChildRun runNestedCheck(const std::string& argument) {
    ChildRun run;
    const char* base = SDL_GetBasePath(); // owned by SDL; null when the platform cannot tell
    REQUIRE(base != nullptr);
    std::filesystem::path exe = std::filesystem::path(base) / "worker_pool_nested_check";
    if (!std::filesystem::exists(exe)) {
        exe += ".exe";
    }
    REQUIRE(std::filesystem::exists(exe));
    const std::string exeText = exe.string();
    const char* args[] = {exeText.c_str(), argument.c_str(), nullptr};
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, static_cast<void*>(args));
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (process == nullptr) {
        return run;
    }
    run.started = true;
    // Reads the output to its end, which is the process ending, and takes its exit code.
    std::size_t size = 0;
    void* output = SDL_ReadProcess(process, &size, &run.exitCode);
    if (output != nullptr) {
        run.output.assign(static_cast<const char*>(output), size);
        SDL_free(output);
    }
    SDL_DestroyProcess(process);
    return run;
}

} // namespace

TEST_CASE("a worker pool runs every index once, with one thread or several", "[core][workers]") {
    // Expected: docs/engine/render-threads.md#workers: every body runs exactly once and the result depends on the
    // inputs alone, so one thread and four give the same output, for a loop too short to share (3), one of a few
    // chunks (40) and a long one (1000).
    for (const std::size_t count : {std::size_t{0}, std::size_t{3}, std::size_t{40}, std::size_t{1000}}) {
        const LoopResult one = runLoop(1, count);
        const LoopResult four = runLoop(4, count);
        CHECK(one.values == four.values);
        CHECK(four.runs == std::vector<int>(count, 1));
        CHECK(one.runs == std::vector<int>(count, 1));
    }
}

TEST_CASE("a worker pool runs loop after loop on the same threads", "[core][workers]") {
    // Expected: docs/engine/render-threads.md#workers: the threads are kept between loops, and each loop returns only
    // when all its bodies have run: a frame poses its humans with one loop and the next frame with another.
    WorkerPool pool(4);
    for (std::size_t loop = 0; loop < 50; ++loop) {
        std::vector<std::size_t> seen(200, 0);
        pool.parallelFor(seen.size(), [&seen, loop](std::size_t index) { seen[index] = index + loop; });
        for (std::size_t i = 0; i < seen.size(); ++i) {
            REQUIRE(seen[i] == i + loop);
        }
    }
}

TEST_CASE("a worker pool of several threads shares a long loop among them", "[core][workers]") {
    // Expected: docs/engine/render-threads.md#workers: a pool of four runs a long loop on its workers as well as the
    // caller. The first body waits (a bounded number of yields, seconds at most) until a second thread has run one,
    // which only a worker can do while the caller is held in it.
    WorkerPool pool(4);
    CHECK(pool.threads() == 4);
    std::mutex mutex;
    std::set<std::thread::id> threads;
    std::atomic<bool> second{false};
    pool.parallelFor(200, [&](std::size_t index) {
        {
            const std::lock_guard lock(mutex);
            threads.insert(std::this_thread::get_id());
            if (threads.size() > 1) {
                second = true;
            }
        }
        if (index == 0) {
            for (int spin = 0; spin < 20'000'000 && !second; ++spin) {
                std::this_thread::yield();
            }
        }
    });
    CHECK(threads.size() > 1);
}

TEST_CASE("a worker pool whose workers cannot all start runs on those that did", "[core][workers]") {
    // Expected: docs/engine/render-threads.md#workers: a worker the system refuses to start is no failure: the pool
    // runs on the workers that started, or on the caller alone when none did, and every body still runs once.
    for (const std::size_t refusedFrom : {std::size_t{0}, std::size_t{1}, std::size_t{2}}) {
        INFO("workers refused from " << refusedFrom);
        WorkerPool pool(4, [refusedFrom](std::size_t worker) { return worker < refusedFrom; });
        CHECK(pool.threads() == refusedFrom + 1);
        std::vector<int> runs(500, 0);
        pool.parallelFor(runs.size(), [&runs](std::size_t index) { ++runs[index]; });
        CHECK(runs == std::vector<int>(runs.size(), 1));
    }
}

TEST_CASE("a worker pool runs on the workers that started when the system refuses a thread", "[core][workers]") {
    // Expected: docs/engine/render-threads.md#workers, docs/guides/conventions.md#errors: std::thread reports a thread
    // the system refuses only by throwing std::system_error; the pool catches it at the call and runs on the workers
    // that started. The start hook throws it where std::thread would, from the third worker on.
    WorkerPool pool(4, [](std::size_t worker) {
        if (worker >= 2) {
            throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
        }
        return true;
    });
    CHECK(pool.threads() == 3);
    std::vector<int> runs(500, 0);
    pool.parallelFor(runs.size(), [&runs](std::size_t index) { ++runs[index]; });
    CHECK(runs == std::vector<int>(runs.size(), 1));
}

TEST_CASE("a loop started from inside a body hits the assert whatever its size, before it takes any lock",
          "[core][workers]") {
    // Expected: docs/engine/render-threads.md#workers: a body that starts a loop is a broken invariant
    // (CONEY_ASSERT), for a nested loop of any size. Of more than 16 items it used to reach the caller's mutex first
    // (the caller locked its own mutex twice, a worker waited on it for ever) and never the assert. A broken
    // invariant ends the process, so each run is a child process (tests/support/worker_pool_nested_check.cpp): an
    // outer loop of 40 on a pool of four whose bodies start a loop of the size asked. It must end by the assert's
    // abort with the assert's message, whichever thread started the loop, for sizes below, at and above the 16 the
    // pool runs on the caller alone.
    // The control: with no nested loop the same child ends by itself, so the helper itself works.
    const ChildRun control = runNestedCheck("none");
    REQUIRE(control.started);
    INFO(control.output);
    CHECK(control.exitCode == 0);
    CHECK(control.output.find("done") != std::string::npos);
    for (const char* nested : {"0", "3", "16", "17", "40"}) {
        INFO("nested loop of " << nested);
        const ChildRun run = runNestedCheck(nested);
        REQUIRE(run.started);
        INFO(run.output);
        CHECK(run.exitCode != 0);
        CHECK(run.output.find("Assertion failed: !tInBody") != std::string::npos);
        CHECK(run.output.find("done") == std::string::npos);
    }
}

TEST_CASE("the shared pool's size is half the hardware threads, between 1 and 8", "[core][workers]") {
    // Expected: docs/engine/render-threads.md#workers: the performance sweep's choice (#1542), half the hardware
    // threads (about the physical cores), at most 8, and one thread when the count is unknown.
    CHECK(coney::defaultWorkerThreads(0) == 1);
    CHECK(coney::defaultWorkerThreads(1) == 1);
    CHECK(coney::defaultWorkerThreads(2) == 1);
    CHECK(coney::defaultWorkerThreads(8) == 4);
    CHECK(coney::defaultWorkerThreads(12) == 6);
    CHECK(coney::defaultWorkerThreads(32) == 8);
}
