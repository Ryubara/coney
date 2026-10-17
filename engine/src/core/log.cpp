#include "core/log.hpp"

#include <cstdio>

namespace coney::core {

std::string_view level_name(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Off:
        return "OFF";
    }
    return "?";
}

std::optional<LogLevel> parse_level(std::string_view name) noexcept {
    if (name == "debug") {
        return LogLevel::Debug;
    }
    if (name == "info") {
        return LogLevel::Info;
    }
    if (name == "warning" || name == "warn") {
        return LogLevel::Warning;
    }
    if (name == "error") {
        return LogLevel::Error;
    }
    if (name == "off") {
        return LogLevel::Off;
    }
    return std::nullopt;
}

std::string format_log_line(Nanoseconds since_start, LogLevel level, std::string_view category,
                            std::string_view message) {
    const auto ns = since_start.count() < 0 ? 0 : since_start.count();
    const auto seconds = ns / 1'000'000'000;
    const auto micros = (ns % 1'000'000'000) / 1'000;
    std::string line = std::format("[{:6}.{:06}] {:<7} {}: ", seconds, micros, level_name(level), category);
    // One message, one line: embedded line breaks would split it.
    for (char c : message) {
        line.push_back(c == '\n' || c == '\r' ? ' ' : c);
    }
    while (!line.empty() && line.back() == ' ') {
        line.pop_back();
    }
    return line;
}

void StderrSink::write(std::string_view line) {
    std::fprintf(stderr, "%.*s\n", static_cast<int>(line.size()), line.data());
}

std::unique_ptr<FileSink> FileSink::open(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "a");
    if (file == nullptr) {
        return nullptr;
    }
    return std::unique_ptr<FileSink>(new FileSink(file));
}

FileSink::~FileSink() { std::fclose(file_); }

void FileSink::write(std::string_view line) {
    std::fprintf(file_, "%.*s\n", static_cast<int>(line.size()), line.data());
    std::fflush(file_);
}

Logger::Logger(const Clock& clock, LogLevel level) : clock_(clock), origin_(clock.now()), level_(level) {}

void Logger::add_sink(std::unique_ptr<LogSink> sink) {
    std::lock_guard lock(mutex_);
    sinks_.push_back(sink.get());
    owned_.push_back(std::move(sink));
}

void Logger::add_sink(LogSink& sink) {
    std::lock_guard lock(mutex_);
    sinks_.push_back(&sink);
}

void Logger::write(LogLevel level, std::string_view category, std::string_view message) {
    if (!enabled(level)) {
        return;
    }
    const std::string line = format_log_line(clock_.now() - origin_, level, category, message);
    std::lock_guard lock(mutex_);
    for (LogSink* sink : sinks_) {
        sink->write(line);
    }
}

} // namespace coney::core
