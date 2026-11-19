// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/light_trace.h"

namespace coney::debug {

bool TraceCall::glows() const { return false; }

std::expected<std::vector<TraceCall>, Error> parseLightTrace(std::string_view, std::string_view, std::string_view) {
    return std::vector<TraceCall>{};
}

std::expected<std::vector<TraceCall>, Error> readLightTrace(const std::filesystem::path&) {
    return std::vector<TraceCall>{};
}

std::string_view traceCallerName(std::uint32_t) { return {}; }

std::vector<TraceLight> replayCall(const TraceCall&) { return {}; }

bool sameLight(const TraceLight&, const TraceLight&) { return false; }

LightReplay replayLightTrace(std::span<const TraceCall>) { return {}; }

std::string lightReplayReport(const LightReplay&) { return {}; }

} // namespace coney::debug
