// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/humans_trace.h"

#include <format>

namespace coney::platform {

std::expected<std::shared_ptr<HumansTrace>, Error> HumansTrace::open(const std::string& path) {
    std::shared_ptr<HumansTrace> trace(new HumansTrace());
    // Text written as is (no newline translation), so a trace reads the same on every platform.
    trace->m_file.open(path, std::ios::binary | std::ios::trunc);
    if (!trace->m_file) {
        return fail(ErrorCode::Io, std::format("cannot write the trace {}", path));
    }
    trace->m_file << ai::FightTracer::header();
    return trace;
}

void HumansTrace::write(const ai::Brains& brains) { m_file << m_tracer.rows(++m_steps, brains); }

} // namespace coney::platform
