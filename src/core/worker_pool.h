// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace coney {

/// The engine's worker threads (docs/engine/render-threads.md#workers): a few threads kept between jobs that, with
/// the calling thread, run the bodies of one parallel loop and return when every body has run. Presentation only: what
/// runs on it reads state nobody writes during the loop and writes only its own index's result, so a result depends on
/// the inputs alone, never on the number of threads or which thread took which index, and test mode, lockstep and
/// networked play see the same output with one thread or many. The game's simulation never runs on it.
///
/// One loop runs at a time: a second caller waits for the first to finish, and a body must not start a loop, of any
/// size, on any pool (CONEY_ASSERT, checked first thing in parallelFor(), before any lock). Threads start with the
/// pool and stop when it is destroyed.
class WorkerPool {
  public:
    /// A pool of `threads` threads counting the caller (at least 1), so `threads - 1` workers; 1 runs every loop on
    /// the calling thread. A worker the system cannot start leaves the pool on those that did (threads() says how
    /// many), none at worst, never a failure. `mayStart`, for tests, is asked before each worker starts and refuses
    /// it as the system would: by returning false, or by throwing std::system_error as std::thread does.
    explicit WorkerPool(std::size_t threads, const std::function<bool(std::size_t worker)>& mayStart = {});
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    WorkerPool(WorkerPool&&) = delete;
    WorkerPool& operator=(WorkerPool&&) = delete;

    /// The threads a loop runs on, the caller's included: fewer than asked when some workers could not start.
    [[nodiscard]] std::size_t threads() const;

    /// Runs `body(index)` once for every index below `count`, on the calling thread and the workers, and returns when
    /// all have run. Indices are taken in small chunks, so the order they run in is not fixed: each body must touch
    /// only what its index owns and read only what no body writes. A short loop runs on the calling thread alone. A
    /// body that calls this, whatever the sizes, is a broken invariant (CONEY_ASSERT).
    void parallelFor(std::size_t count, const std::function<void(std::size_t index)>& body);

  private:
    class Workers;
    std::unique_ptr<Workers> m_workers; // null with one thread
    std::size_t m_threads = 1;
};

/// How many threads the shared pool runs on, the caller's included, for a machine of `hardwareThreads` hardware
/// threads (0 when unknown): half of them, roughly the physical cores, so a worker does not share a core with the main
/// thread's own hyperthread; at least 1 and at most 8, the performance sweep's choice (#1542).
[[nodiscard]] std::size_t defaultWorkerThreads(unsigned hardwareThreads);

/// The engine's one pool, sized by defaultWorkerThreads() of this machine and started at its first use, so a process
/// that never runs a parallel loop starts no threads. Lives until the process exits.
[[nodiscard]] WorkerPool& sharedWorkerPool();

} // namespace coney
