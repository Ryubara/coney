// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "core/error.h"
#include "core/log_filter.h"

namespace coney::platform {

/// Where this run's log (core/log.h) goes: the console, and a log file when there is one. Lines at warn and error go
/// to stderr, the rest to stdout, each stamped; the file gets the same lines with the game's own text withheld
/// (logging::withheld()), and the ring's dump on a bug report, a failed assertion or a crash. While it lives it also
/// holds crash handlers (SIGSEGV, SIGILL, SIGFPE, SIGABRT) that dump the ring before the process ends. Coney's own:
/// docs/guides/run-log.md.
class LogOutput {
  public:
    /// Starts the log's outputs with `filter`: the console, and the file at `file` when given. With `keepPrevious` an
    /// existing file is first renamed to `<name>.prev<ext>` (`coney.prev.log`), so the run before a crash survives
    /// the next start. Fails with ErrorCode::Io when the file cannot be opened; the console works either way.
    [[nodiscard]] static std::expected<std::unique_ptr<LogOutput>, Error>
    start(const std::optional<std::filesystem::path>& file, const logging::LogFilter& filter, bool keepPrevious);

    LogOutput(const LogOutput&) = delete;
    LogOutput& operator=(const LogOutput&) = delete;
    LogOutput(LogOutput&&) = delete;
    LogOutput& operator=(LogOutput&&) = delete;
    /// Stops the outputs (the logger keeps its ring, with no sinks), restores the crash handlers and closes the file.
    ~LogOutput();

    /// The log file, when there is one.
    [[nodiscard]] const std::optional<std::filesystem::path>& file() const { return m_path; }
    /// The debug menus' bug report: the ring dumped into the log file (to stderr without one) and flushed. Returns
    /// the line to show the person who asked: how many events and where.
    std::string writeBugReport();

  private:
    LogOutput() = default;

    std::optional<std::filesystem::path> m_path;
    std::FILE* m_file = nullptr;
};

/// `coney.log` in SDL's preference folder for Coney, where a normal run writes its log; empty when SDL cannot give
/// the folder.
[[nodiscard]] std::filesystem::path defaultLogPath();

/// A path as Coney writes it into the log: the one place that does, so how paths read in a log (attached to public
/// issues) is decided here.
[[nodiscard]] std::string logPathText(const std::filesystem::path& path);

} // namespace coney::platform
