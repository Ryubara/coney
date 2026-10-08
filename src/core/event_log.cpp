// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/event_log.h"

#include <format>
#include <utility>

namespace coney::events {

namespace {

// The open log; one per process (a run has one game).
struct Log {
    Sink sink;
    std::uint64_t step = 0;
};

// The process's log, made on first use.
Log& theLog() {
    static Log log;
    return log;
}

// A CSV field, quoted only when it must be.
std::string csvField(std::string_view text) {
    if (text.find_first_of(",\"\r\n") == std::string_view::npos) {
        return std::string(text);
    }
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') {
            out += '"';
        }
        out += c;
    }
    out += '"';
    return out;
}

} // namespace

void setSink(Sink sink) { theLog().sink = std::move(sink); }

bool enabled() { return static_cast<bool>(theLog().sink); }

void setStep(std::uint64_t step) { theLog().step = step; }

std::uint64_t step() { return theLog().step; }

void emit(std::string_view kind, std::string_view name, std::string_view detail) {
    const Log& log = theLog();
    if (log.sink) {
        log.sink(log.step, kind, name, detail);
    }
}

std::string csvLine(std::uint64_t step, std::string_view kind, std::string_view name, std::string_view detail) {
    return std::format("{},{},{},{}\n", step, csvField(kind), csvField(name), csvField(detail));
}

} // namespace coney::events
