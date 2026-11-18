// SPDX-License-Identifier: GPL-3.0-or-later
// Stub so the GPU timer's tests compile before the timer exists: every call does nothing and reports nothing.
#include "platform/gpu_timer.h"

#include <utility>

namespace coney::platform {

GpuTimer::GpuTimer(GpuTimerApi api) : m_api(std::move(api)) {}
GpuTimer::~GpuTimer() = default;
void GpuTimer::begin() {}
void GpuTimer::end() {}
std::optional<std::uint64_t> GpuTimer::takeResult() { return std::nullopt; }

} // namespace coney::platform
