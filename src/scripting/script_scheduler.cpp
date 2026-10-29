// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the scheduler is Coney's seam over the script system; the original calls its script system directly
#include "scripting/script_scheduler.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include "core/assert.h"
#include "core/perf.h"
#include "scripting/script_system.h"

namespace coney::script {

namespace {

// An entry of `kind` for the factories below; the other fields keep their defaults.
ScriptEntry entryOf(ScriptEntryKind kind) {
    ScriptEntry entry;
    entry.kind = kind;
    return entry;
}

// The clock the scheduler reads unless a test gives its own: the profiler's, which the platform installs (core reads
// no clock of its own, docs/guides/conventions.md#update-and-render); 0 with none.
std::chrono::nanoseconds profilerNow() { return std::chrono::nanoseconds(static_cast<std::int64_t>(perf::now())); }

} // namespace

std::string_view scriptEntryKindName(ScriptEntryKind kind) {
    static constexpr std::array<std::string_view, 9> kNames{"files", "file",    "level entry",   "preloads", "frame",
                                                            "call",  "message", "anim callback", "console"};
    return kNames.at(static_cast<std::size_t>(kind));
}

ScriptEntry ScriptEntry::fileList(std::span<const std::string_view> names) {
    ScriptEntry entry = entryOf(ScriptEntryKind::Files);
    entry.files = names;
    return entry;
}

ScriptEntry ScriptEntry::file(std::string_view name) {
    ScriptEntry entry = entryOf(ScriptEntryKind::File);
    entry.name = name;
    return entry;
}

ScriptEntry ScriptEntry::levelEntry(std::string_view level) {
    ScriptEntry entry = entryOf(ScriptEntryKind::LevelEntry);
    entry.name = level;
    return entry;
}

ScriptEntry ScriptEntry::preloads(bool all) {
    ScriptEntry entry = entryOf(ScriptEntryKind::Preloads);
    entry.all = all;
    return entry;
}

ScriptEntry ScriptEntry::frame(std::uint64_t nowMs, double stepSeconds) {
    ScriptEntry entry = entryOf(ScriptEntryKind::Frame);
    entry.nowMs = nowMs;
    entry.stepSeconds = stepSeconds;
    return entry;
}

ScriptEntry ScriptEntry::call(std::string_view name, std::span<const Value> args) {
    ScriptEntry entry = entryOf(ScriptEntryKind::Call);
    entry.name = name;
    entry.args = args;
    return entry;
}

ScriptEntry ScriptEntry::message(std::string_view name, std::span<const Value> args) {
    ScriptEntry entry = call(name, args);
    entry.kind = ScriptEntryKind::Message;
    return entry;
}

ScriptEntry ScriptEntry::animCallback(std::string_view name, std::span<const Value> args) {
    ScriptEntry entry = call(name, args);
    entry.kind = ScriptEntryKind::AnimCallback;
    return entry;
}

ScriptEntry ScriptEntry::console(const std::function<void(LuaVm&)>& body) {
    ScriptEntry entry = entryOf(ScriptEntryKind::Console);
    entry.body = &body;
    return entry;
}

ScriptScheduler::ScriptScheduler(ScriptSystem& scripts) : m_scripts(scripts) {}

std::optional<std::vector<Value>> ScriptScheduler::run(const ScriptEntry& entry) {
    // Each kind goes to the script system's operation for it, which opens the entry itself (enter()), so a run that
    // comes through one of those operations directly is an entry all the same.
    switch (entry.kind) {
    case ScriptEntryKind::Files:
        m_scripts.runFiles(entry.files);
        return std::vector<Value>{};
    case ScriptEntryKind::File:
        if (!m_scripts.runFile(entry.name)) {
            return std::nullopt;
        }
        return std::vector<Value>{};
    case ScriptEntryKind::LevelEntry:
        m_scripts.enterLevel(entry.name);
        return std::vector<Value>{};
    case ScriptEntryKind::Preloads:
        m_scripts.servicePreloads(entry.all);
        return std::vector<Value>{};
    case ScriptEntryKind::Frame:
        m_scripts.update(entry.nowMs, entry.stepSeconds);
        return std::vector<Value>{};
    case ScriptEntryKind::Call:
    case ScriptEntryKind::Message:
    case ScriptEntryKind::AnimCallback:
        return m_scripts.callEntry(entry);
    case ScriptEntryKind::Console: {
        // Coney's debug menus run their line or call in the state, as one entry.
        const Scope scope(*this, entry);
        if (entry.body != nullptr && *entry.body && m_scripts.exists()) {
            (*entry.body)(m_scripts.vm());
        }
        return std::vector<Value>{};
    }
    }
    return std::nullopt;
}

const ProjectTime* ScriptScheduler::projectTime(std::string_view project) const {
    const auto found = std::ranges::find(m_times, project, &ProjectTime::project);
    return found == m_times.end() ? nullptr : &*found;
}

void ScriptScheduler::setClock(Clock clock) { m_clock = std::move(clock); }

// Opens the entry: tells the watcher, and starts the clock for an outermost one.
ScriptScheduler::Scope::Scope(ScriptScheduler& scheduler, const ScriptEntry& entry)
    : m_scheduler(scheduler), m_kind(entry.kind), m_project(entry.project) {
    if (m_scheduler.m_watcher) {
        m_scheduler.m_watcher(entry, m_scheduler.m_depth);
    }
    // Only the outermost entry is timed: a nested one's time is already inside it.
    if (m_scheduler.m_depth++ == 0) {
        m_scheduler.m_outerStart = m_scheduler.now();
    }
}

// Closes the entry: an outermost one adds its time to its project, and a frame ends the step.
ScriptScheduler::Scope::~Scope() {
    CONEY_ASSERT(m_scheduler.m_depth > 0);
    if (--m_scheduler.m_depth > 0) {
        return;
    }
    ProjectTime& time = m_scheduler.timeOf(m_project);
    time.thisStep += m_scheduler.now() - m_scheduler.m_outerStart;
    // The scripts' frame is the last script run of a step: it closes the step's record.
    if (m_kind == ScriptEntryKind::Frame) {
        time.lastStep = std::exchange(time.thisStep, std::chrono::nanoseconds{});
        ++time.steps;
    }
}

ProjectTime& ScriptScheduler::timeOf(std::string_view project) {
    const auto found = std::ranges::find(m_times, project, &ProjectTime::project);
    if (found != m_times.end()) {
        return *found;
    }
    ProjectTime time;
    time.project = std::string(project);
    m_times.push_back(std::move(time));
    return m_times.back();
}

std::chrono::nanoseconds ScriptScheduler::now() const { return m_clock ? m_clock() : profilerNow(); }

} // namespace coney::script
