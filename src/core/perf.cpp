// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/perf.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <format>
#include <utility>

namespace coney::perf {

namespace {

// A counter: its name, its sampler and the total it gave at the last frame's end.
struct Counter {
    std::string name;
    std::function<std::uint64_t()> sample;
    std::uint64_t last = 0;
};

// Everything the zones and the recorder share. One instance, on the main thread.
struct State {
    Clock clock = nullptr;
    bool enabled = false;
    std::vector<std::string> zoneNames;
    std::vector<std::uint64_t> zoneTimes; // the frame being measured, by zone index
    std::vector<Counter> counters;
    std::uint64_t frameStart = 0; // when the frame being measured began (the last endFrame); 0: none yet
    bool recording = false;
    // The recorded frames, flat so that recording allocates nothing within its reserve: each frame's length, and per
    // frame where its zone and counter values start in `values` and how many there are.
    struct Kept {
        std::uint64_t frameNanoseconds;
        std::size_t first;
        std::size_t zones;
        std::size_t counters;
    };
    std::vector<Kept> kept;
    std::vector<std::uint64_t> values;
    std::vector<FrameRecord> recent; // a ring of the last kRecentFrames
    std::size_t recentNext = 0;
};

// The one state, made on first use, so a zone site's static initialiser can register into it in any order.
State& state() {
    static State instance;
    return instance;
}

// A percentile by nearest rank of sorted `values` (not empty), `p` in (0, 100].
double nearestRank(const std::vector<double>& values, double p) {
    const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(values.size())));
    return values[std::clamp<std::size_t>(rank, 1, values.size()) - 1];
}

// A JSON string literal of `text` (names and metadata: printable ASCII, but quotes and backslashes are escaped).
std::string jsonString(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += std::format("\\u{:04x}", static_cast<unsigned>(c));
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// A spread as a JSON object, three decimals.
std::string jsonSpread(const Spread& spread) {
    return std::format(R"({{"mean": {:.3f}, "p50": {:.3f}, "p95": {:.3f}, "p99": {:.3f}, "max": {:.3f}}})", spread.mean,
                       spread.p50, spread.p95, spread.p99, spread.max);
}

} // namespace

void setClock(Clock clock) { state().clock = clock; }

bool enabled() { return state().enabled; }

void setEnabled(bool on) {
    State& s = state();
    if (on && !s.enabled) {
        std::ranges::fill(s.zoneTimes, 0);
        s.frameStart = 0;
        for (Counter& counter : s.counters) {
            counter.last = counter.sample();
        }
    }
    s.enabled = on;
}

std::uint64_t now() {
    const Clock clock = state().clock;
    return clock != nullptr ? clock() : 0;
}

ZoneSite::ZoneSite(std::string_view name) {
    State& s = state();
    for (std::size_t i = 0; i < s.zoneNames.size(); ++i) {
        if (s.zoneNames[i] == name) {
            m_index = i;
            return;
        }
    }
    m_index = s.zoneNames.size();
    s.zoneNames.emplace_back(name);
    s.zoneTimes.push_back(0);
}

void addZoneTime(std::size_t index, std::uint64_t nanoseconds) {
    State& s = state();
    if (index < s.zoneTimes.size()) {
        s.zoneTimes[index] += nanoseconds;
    }
}

std::vector<std::string> zoneNames() { return state().zoneNames; }

std::size_t addCounter(std::string_view name, std::function<std::uint64_t()> sample) {
    State& s = state();
    const std::uint64_t total = sample();
    for (std::size_t i = 0; i < s.counters.size(); ++i) {
        if (s.counters[i].name == name) {
            s.counters[i].sample = std::move(sample);
            s.counters[i].last = total;
            return i;
        }
    }
    s.counters.push_back(Counter{std::string(name), std::move(sample), total});
    return s.counters.size() - 1;
}

void clearCounters() { state().counters.clear(); }

std::vector<std::string> counterNames() {
    std::vector<std::string> names;
    for (const Counter& counter : state().counters) {
        names.push_back(counter.name);
    }
    return names;
}

void endFrame() {
    State& s = state();
    if (!s.enabled) {
        return;
    }
    const std::uint64_t end = now();
    // The record is written into a slot that already has room (the ring, then the reserved list), so a frame
    // allocates nothing once the ring is full and the zones and counters are all known.
    FrameRecord* record = nullptr;
    if (s.recent.size() < kRecentFrames) {
        s.recent.emplace_back();
        record = &s.recent.back();
    } else {
        record = &s.recent[s.recentNext];
    }
    s.recentNext = (s.recentNext + 1) % kRecentFrames;
    record->frameNanoseconds = s.frameStart != 0 && end > s.frameStart ? end - s.frameStart : 0;
    record->zones.assign(s.zoneTimes.begin(), s.zoneTimes.end());
    record->counters.resize(s.counters.size());
    for (std::size_t i = 0; i < s.counters.size(); ++i) {
        Counter& counter = s.counters[i];
        const std::uint64_t total = counter.sample();
        record->counters[i] = total - counter.last;
        counter.last = total;
    }
    if (s.recording) {
        s.kept.push_back(State::Kept{record->frameNanoseconds, s.values.size(), record->zones.size(),
                                     record->counters.size()});
        s.values.insert(s.values.end(), record->zones.begin(), record->zones.end());
        s.values.insert(s.values.end(), record->counters.begin(), record->counters.end());
    }
    std::ranges::fill(s.zoneTimes, 0);
    s.frameStart = end;
}

void startRecording(std::size_t reserve) {
    State& s = state();
    s.kept.clear();
    s.values.clear();
    s.kept.reserve(reserve);
    // Room for every zone and counter known now, and as many again for those that first run while recording.
    s.values.reserve(reserve * 2 * (s.zoneNames.size() + s.counters.size() + 8));
    s.recording = true;
}

void stopRecording() { state().recording = false; }

std::vector<FrameRecord> recorded() {
    const State& s = state();
    std::vector<FrameRecord> records;
    records.reserve(s.kept.size());
    for (const State::Kept& kept : s.kept) {
        const auto first = s.values.begin() + static_cast<std::ptrdiff_t>(kept.first);
        const auto middle = first + static_cast<std::ptrdiff_t>(kept.zones);
        records.push_back(FrameRecord{kept.frameNanoseconds, std::vector<std::uint64_t>(first, middle),
                                      std::vector<std::uint64_t>(middle, middle + static_cast<std::ptrdiff_t>(kept.counters))});
    }
    return records;
}

RecentMeans recentMeans() {
    const State& s = state();
    RecentMeans means;
    means.zoneMilliseconds.assign(s.zoneNames.size(), 0.0);
    means.counters.assign(s.counters.size(), 0.0);
    std::size_t timed = 0;
    for (const FrameRecord& record : s.recent) {
        if (record.frameNanoseconds != 0) {
            means.frameMilliseconds += static_cast<double>(record.frameNanoseconds) / 1e6;
            ++timed;
        }
        for (std::size_t i = 0; i < record.zones.size() && i < means.zoneMilliseconds.size(); ++i) {
            means.zoneMilliseconds[i] += static_cast<double>(record.zones[i]) / 1e6;
        }
        for (std::size_t i = 0; i < record.counters.size() && i < means.counters.size(); ++i) {
            means.counters[i] += static_cast<double>(record.counters[i]);
        }
    }
    means.frames = s.recent.size();
    if (timed != 0) {
        means.frameMilliseconds /= static_cast<double>(timed);
    }
    if (means.frames != 0) {
        const auto frames = static_cast<double>(means.frames);
        for (double& value : means.zoneMilliseconds) {
            value /= frames;
        }
        for (double& value : means.counters) {
            value /= frames;
        }
    }
    return means;
}

Spread spreadOf(std::vector<double> values) {
    Spread spread;
    if (values.empty()) {
        return spread;
    }
    std::ranges::sort(values);
    double total = 0.0;
    for (const double value : values) {
        total += value;
    }
    spread.mean = total / static_cast<double>(values.size());
    spread.p50 = nearestRank(values, 50.0);
    spread.p95 = nearestRank(values, 95.0);
    spread.p99 = nearestRank(values, 99.0);
    spread.max = values.back();
    return spread;
}

Summary summarise() {
    const State& s = state();
    const std::vector<FrameRecord> records = recorded();
    Summary summary;
    summary.frames = records.size();
    std::vector<double> frameTimes;
    for (const FrameRecord& record : records) {
        if (record.frameNanoseconds != 0) {
            frameTimes.push_back(static_cast<double>(record.frameNanoseconds) / 1e6);
        }
    }
    summary.frameMilliseconds = spreadOf(std::move(frameTimes));
    for (std::size_t zone = 0; zone < s.zoneNames.size(); ++zone) {
        std::vector<double> values;
        values.reserve(records.size());
        for (const FrameRecord& record : records) {
            values.push_back(zone < record.zones.size() ? static_cast<double>(record.zones[zone]) / 1e6 : 0.0);
        }
        summary.zoneMilliseconds.emplace_back(s.zoneNames[zone], spreadOf(std::move(values)));
    }
    for (std::size_t counter = 0; counter < s.counters.size(); ++counter) {
        std::vector<double> values;
        values.reserve(records.size());
        for (const FrameRecord& record : records) {
            values.push_back(counter < record.counters.size() ? static_cast<double>(record.counters[counter]) : 0.0);
        }
        summary.counters.emplace_back(s.counters[counter].name, spreadOf(std::move(values)));
    }
    return summary;
}

std::string toJson(const Summary& summary, const std::vector<std::pair<std::string, std::string>>& meta) {
    std::string out = "{\n  \"meta\": {";
    for (std::size_t i = 0; i < meta.size(); ++i) {
        out += std::format("{}{}: {}", i == 0 ? "" : ", ", jsonString(meta[i].first), jsonString(meta[i].second));
    }
    out += std::format("}},\n  \"frames\": {},\n  \"frame_ms\": {},\n  \"zones_ms\": {{", summary.frames,
                       jsonSpread(summary.frameMilliseconds));
    for (std::size_t i = 0; i < summary.zoneMilliseconds.size(); ++i) {
        out += std::format("{}\n    {}: {}", i == 0 ? "" : ",", jsonString(summary.zoneMilliseconds[i].first),
                           jsonSpread(summary.zoneMilliseconds[i].second));
    }
    out += "\n  },\n  \"counters\": {";
    for (std::size_t i = 0; i < summary.counters.size(); ++i) {
        out += std::format("{}\n    {}: {}", i == 0 ? "" : ",", jsonString(summary.counters[i].first),
                           jsonSpread(summary.counters[i].second));
    }
    out += "\n  }\n}\n";
    return out;
}

void resetForTests() {
    State& s = state();
    s.enabled = false;
    s.clock = nullptr;
    std::ranges::fill(s.zoneTimes, 0);
    s.counters.clear();
    s.frameStart = 0;
    s.recording = false;
    s.kept.clear();
    s.values.clear();
    s.recent.clear();
    s.recentNext = 0;
}

} // namespace coney::perf
