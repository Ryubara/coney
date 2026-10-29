// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the test commit: the scheduler runs nothing yet.
#include "scripting/script_scheduler.h"

#include "scripting/script_system.h"

namespace coney::script {

std::string_view scriptEntryKindName(ScriptEntryKind /*kind*/) { return {}; }

ScriptEntry ScriptEntry::fileList(std::span<const std::string_view> names) {
    return ScriptEntry{.kind = ScriptEntryKind::Files, .files = names};
}
ScriptEntry ScriptEntry::file(std::string_view name) {
    return ScriptEntry{.kind = ScriptEntryKind::File, .name = name};
}
ScriptEntry ScriptEntry::levelEntry(std::string_view level) {
    return ScriptEntry{.kind = ScriptEntryKind::LevelEntry, .name = level};
}
ScriptEntry ScriptEntry::preloads(bool all) { return ScriptEntry{.kind = ScriptEntryKind::Preloads, .all = all}; }
ScriptEntry ScriptEntry::frame(std::uint64_t nowMs, double stepSeconds) {
    return ScriptEntry{.kind = ScriptEntryKind::Frame, .nowMs = nowMs, .stepSeconds = stepSeconds};
}
ScriptEntry ScriptEntry::call(std::string_view name, std::span<const Value> args) {
    return ScriptEntry{.kind = ScriptEntryKind::Call, .name = name, .args = args};
}
ScriptEntry ScriptEntry::message(std::string_view name, std::span<const Value> args) {
    return ScriptEntry{.kind = ScriptEntryKind::Message, .name = name, .args = args};
}
ScriptEntry ScriptEntry::animCallback(std::string_view name, std::span<const Value> args) {
    return ScriptEntry{.kind = ScriptEntryKind::AnimCallback, .name = name, .args = args};
}
ScriptEntry ScriptEntry::console(const std::function<void(LuaVm&)>& body) {
    return ScriptEntry{.kind = ScriptEntryKind::Console, .body = &body};
}

ScriptScheduler::ScriptScheduler(ScriptSystem& scripts) : m_scripts(scripts) {}

std::optional<std::vector<Value>> ScriptScheduler::run(const ScriptEntry& /*entry*/) { return std::nullopt; }

const ProjectTime* ScriptScheduler::projectTime(std::string_view /*project*/) const { return nullptr; }

void ScriptScheduler::setClock(Clock clock) { m_clock = std::move(clock); }

ScriptScheduler::Scope::Scope(ScriptScheduler& scheduler, const ScriptEntry& entry)
    : m_scheduler(scheduler), m_kind(entry.kind), m_project(entry.project) {}

ScriptScheduler::Scope::~Scope() = default;

ProjectTime& ScriptScheduler::timeOf(std::string_view project) {
    m_times.push_back(ProjectTime{.project = std::string(project)});
    return m_times.back();
}

std::chrono::nanoseconds ScriptScheduler::now() const { return {}; }

} // namespace coney::script
