#pragma once

#include <atomic>
#include <cstdio>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/clock.hpp"

namespace coney::core {

enum class LogLevel { Debug, Info, Warning, Error, Off };

[[nodiscard]] std::string_view level_name(LogLevel level) noexcept;
// Accepts "debug", "info", "warning" (or "warn"), "error" and "off".
[[nodiscard]] std::optional<LogLevel> parse_level(std::string_view name) noexcept;

// Subsystem names used as log categories. A category is a plain string, so new subsystems need no change here.
namespace log_category {
inline constexpr std::string_view kCore = "core";
inline constexpr std::string_view kLoop = "loop";
inline constexpr std::string_view kPlatform = "platform";
inline constexpr std::string_view kWindow = "window";
inline constexpr std::string_view kGpu = "gpu";
inline constexpr std::string_view kApp = "app";
} // namespace log_category

// Where finished log lines go. A line has no trailing newline.
class LogSink {
  public:
    virtual ~LogSink() = default;
    virtual void write(std::string_view line) = 0;
};

class StderrSink final : public LogSink {
  public:
    void write(std::string_view line) override;
};

// Appends to a file; `open` fails when the file cannot be opened for writing.
class FileSink final : public LogSink {
  public:
    [[nodiscard]] static std::unique_ptr<FileSink> open(const std::string& path);
    ~FileSink() override;
    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;
    void write(std::string_view line) override;

  private:
    explicit FileSink(std::FILE* file) : file_(file) {}
    std::FILE* file_;
};

// Keeps lines in memory, for tests.
class MemorySink final : public LogSink {
  public:
    void write(std::string_view line) override { lines.emplace_back(line); }
    std::vector<std::string> lines;
};

// Formats messages as one line each and hands them to every sink:
//   [    12.345678] INFO    window: opened 1280x720
// The time stamp is seconds since the logger was made, read from its clock. Safe to call from any thread.
class Logger {
  public:
    explicit Logger(const Clock& clock, LogLevel level = LogLevel::Info);

    void add_sink(std::unique_ptr<LogSink> sink);
    void add_sink(LogSink& sink); // not owned; must outlive the logger

    void set_level(LogLevel level) noexcept { level_.store(level, std::memory_order_relaxed); }
    [[nodiscard]] LogLevel level() const noexcept { return level_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool enabled(LogLevel wanted) const noexcept {
        return wanted != LogLevel::Off && wanted >= level();
    }

    void write(LogLevel level, std::string_view category, std::string_view message);

    template <typename... Args>
    void log(LogLevel level, std::string_view category, std::format_string<Args...> format, Args&&... args) {
        if (enabled(level)) {
            write(level, category, std::format(format, std::forward<Args>(args)...));
        }
    }
    template <typename... Args>
    void debug(std::string_view category, std::format_string<Args...> format, Args&&... args) {
        log(LogLevel::Debug, category, format, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void info(std::string_view category, std::format_string<Args...> format, Args&&... args) {
        log(LogLevel::Info, category, format, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void warning(std::string_view category, std::format_string<Args...> format, Args&&... args) {
        log(LogLevel::Warning, category, format, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void error(std::string_view category, std::format_string<Args...> format, Args&&... args) {
        log(LogLevel::Error, category, format, std::forward<Args>(args)...);
    }

  private:
    const Clock& clock_;
    Nanoseconds origin_;
    std::atomic<LogLevel> level_;
    std::mutex mutex_;
    std::vector<std::unique_ptr<LogSink>> owned_;
    std::vector<LogSink*> sinks_;
};

// The text of one log line, without a newline. Exposed so the format has one definition.
[[nodiscard]] std::string format_log_line(Nanoseconds since_start, LogLevel level, std::string_view category,
                                          std::string_view message);

} // namespace coney::core
