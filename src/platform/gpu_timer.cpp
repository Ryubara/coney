// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/gpu_timer.h"

#include <utility>

#include <rw.h>

namespace coney::platform {

GpuTimerApi openGlTimerApi() {
    // The loader leaves a pointer null when the context does not have the call (timer queries are OpenGL 3.3's).
    if (glad_glGenQueries == nullptr || glad_glDeleteQueries == nullptr || glad_glBeginQuery == nullptr ||
        glad_glEndQuery == nullptr || glad_glGetQueryObjectuiv == nullptr || glad_glGetQueryObjectui64v == nullptr) {
        return {};
    }
    GpuTimerApi api;
    api.generate = [](std::size_t count, std::uint32_t* ids) { glGenQueries(static_cast<GLsizei>(count), ids); };
    api.destroy = [](std::size_t count, const std::uint32_t* ids) {
        glDeleteQueries(static_cast<GLsizei>(count), ids);
    };
    api.begin = [](std::uint32_t id) { glBeginQuery(GL_TIME_ELAPSED, id); };
    api.end = [] { glEndQuery(GL_TIME_ELAPSED); };
    api.ready = [](std::uint32_t id) {
        GLuint available = 0;
        glGetQueryObjectuiv(id, GL_QUERY_RESULT_AVAILABLE, &available);
        return available != 0;
    };
    api.result = [](std::uint32_t id) {
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(id, GL_QUERY_RESULT, &nanoseconds);
        return static_cast<std::uint64_t>(nanoseconds);
    };
    return api;
}

// The queries are made at once, so the ring needs no allocation while the bench measures.
GpuTimer::GpuTimer(GpuTimerApi api) : m_api(std::move(api)) {
    if (available()) {
        m_api.generate(kQueries, m_ids.data());
    }
}

// Gives the queries back to the driver; the context that made them must still be current.
GpuTimer::~GpuTimer() {
    if (available()) {
        m_api.destroy(kQueries, m_ids.data());
    }
}

void GpuTimer::begin() {
    if (!available() || m_open) {
        return;
    }
    // Every query still in flight: the GPU is more than a ring behind, so this frame goes unmeasured.
    if (m_pending == kQueries) {
        ++m_skipped;
        return;
    }
    m_api.begin(m_ids.at(m_head));
    m_open = true;
}

void GpuTimer::end() {
    if (!m_open) {
        return;
    }
    m_api.end();
    m_open = false;
    m_head = (m_head + 1) % kQueries;
    ++m_pending;
}

std::optional<std::uint64_t> GpuTimer::takeResult() {
    if (!available() || m_pending == 0) {
        return std::nullopt;
    }
    // The oldest pending query is m_pending slots behind the head.
    const std::size_t oldest = (m_head + kQueries - m_pending) % kQueries;
    const std::uint32_t id = m_ids.at(oldest);
    if (!m_api.ready(id)) {
        return std::nullopt;
    }
    --m_pending;
    return m_api.result(id);
}

} // namespace coney::platform
