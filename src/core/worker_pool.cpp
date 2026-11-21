// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/worker_pool.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/assert.h"

namespace coney {

namespace {

// Indices a thread takes at a time: enough that taking one costs little next to posing eight humans, few enough that
// the threads finish together.
constexpr std::size_t kChunk = 8;

// Whether this thread is running a body of some pool's loop, so a body that starts a loop is caught, not deadlocked.
thread_local bool tInBody = false;

// Marks this thread as running bodies for its lifetime.
class BodyScope {
  public:
    BodyScope() {
        CONEY_ASSERT(!tInBody);
        tInBody = true;
    }
    ~BodyScope() { tInBody = false; }
    BodyScope(const BodyScope&) = delete;
    BodyScope& operator=(const BodyScope&) = delete;
    BodyScope(BodyScope&&) = delete;
    BodyScope& operator=(BodyScope&&) = delete;
};

} // namespace

// The workers and the loop they share. A loop is a generation: the caller publishes the body and the count under the
// mutex and bumps the generation, every worker wakes, takes chunks until none is left and says it is done; the caller
// takes chunks too and returns once every worker has said so, so no worker still reads the body after the loop.
class WorkerPool::Workers {
  public:
    explicit Workers(std::size_t workers) {
        m_threads.reserve(workers);
        for (std::size_t i = 0; i < workers; ++i) {
            m_threads.emplace_back([this] { work(); });
        }
    }
    ~Workers() {
        {
            const std::lock_guard lock(m_mutex);
            m_quit = true;
            ++m_generation;
        }
        m_wake.notify_all();
        for (std::thread& thread : m_threads) {
            thread.join();
        }
    }
    Workers(const Workers&) = delete;
    Workers& operator=(const Workers&) = delete;
    Workers(Workers&&) = delete;
    Workers& operator=(Workers&&) = delete;

    // Runs one loop on every worker and the caller, and waits for all of them; one caller at a time.
    void run(std::size_t count, const std::function<void(std::size_t)>& body) {
        const std::lock_guard caller(m_callerMutex);
        {
            const std::lock_guard lock(m_mutex);
            m_body = &body;
            m_count = count;
            m_next.store(0);
            m_busy = m_threads.size();
            ++m_generation;
        }
        m_wake.notify_all();
        drain();
        std::unique_lock lock(m_mutex);
        m_done.wait(lock, [this] { return m_busy == 0; });
        m_body = nullptr;
    }

  private:
    // Takes chunks of the loop until none is left.
    void drain() {
        const BodyScope scope;
        for (;;) {
            const std::size_t begin = m_next.fetch_add(kChunk);
            if (begin >= m_count) {
                return;
            }
            const std::size_t end = std::min(begin + kChunk, m_count);
            for (std::size_t i = begin; i < end; ++i) {
                (*m_body)(i);
            }
        }
    }

    // A worker: waits for the next loop, drains it, says it is done; stops when the pool goes.
    void work() {
        std::uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this, seen] { return m_generation != seen; });
                seen = m_generation;
                if (m_quit) {
                    return;
                }
            }
            drain();
            {
                const std::lock_guard lock(m_mutex);
                --m_busy;
            }
            m_done.notify_one();
        }
    }

    std::vector<std::thread> m_threads;
    std::mutex m_callerMutex; // one loop at a time
    std::mutex m_mutex;       // the fields below but m_next
    std::condition_variable m_wake;
    std::condition_variable m_done;
    const std::function<void(std::size_t)>* m_body = nullptr;
    std::size_t m_count = 0;
    std::atomic<std::size_t> m_next{0};
    std::size_t m_busy = 0; // workers still in this loop
    std::uint64_t m_generation = 0;
    bool m_quit = false;
};

WorkerPool::WorkerPool(std::size_t threads) {
    if (threads > 1) {
        m_workers = std::make_unique<Workers>(threads - 1);
    }
    m_threads = std::max<std::size_t>(threads, 1);
}

WorkerPool::~WorkerPool() = default;

std::size_t WorkerPool::threads() const { return m_threads; }

void WorkerPool::parallelFor(std::size_t count, const std::function<void(std::size_t index)>& body) {
    // A loop of two chunks or fewer gains less from the workers than waking them costs; on the caller alone, as
    // one thread runs every loop.
    if (m_workers == nullptr || count <= 2 * kChunk) {
        const BodyScope scope;
        for (std::size_t i = 0; i < count; ++i) {
            body(i);
        }
        return;
    }
    m_workers->run(count, body);
}

std::size_t defaultWorkerThreads(unsigned hardwareThreads) {
    return std::clamp<std::size_t>(hardwareThreads / 2, 1, 8);
}

WorkerPool& sharedWorkerPool() {
    static WorkerPool pool(defaultWorkerThreads(std::thread::hardware_concurrency()));
    return pool;
}

} // namespace coney
