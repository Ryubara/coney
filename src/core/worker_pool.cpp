// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/worker_pool.h"

#include <cstddef>
#include <functional>

namespace coney {

// Stub for the tests' commit: every loop on the calling thread, as the posing ran before the pool.
class WorkerPool::Workers {};

WorkerPool::WorkerPool(std::size_t /*threads*/) {}

WorkerPool::~WorkerPool() = default;

std::size_t WorkerPool::threads() const { return 1; }

void WorkerPool::parallelFor(std::size_t count, const std::function<void(std::size_t index)>& body) {
    for (std::size_t i = 0; i < count; ++i) {
        body(i);
    }
}

std::size_t defaultWorkerThreads(unsigned /*hardwareThreads*/) { return 1; }

WorkerPool& sharedWorkerPool() {
    static WorkerPool pool(1);
    return pool;
}

} // namespace coney
