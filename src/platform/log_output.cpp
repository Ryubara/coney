// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/log_output.h"

#include <array>
#include <atomic>
#include <csignal>
#include <format>
#include <string_view>
#include <system_error>
#include <utility>

#include <SDL3/SDL.h>

#include "core/log.h"
#include "core/log_record.h"

namespace coney::platform {

namespace {

// The signals the crash handler dumps the ring for.
constexpr std::array<int, 4> kCrashSignals{SIGSEGV, SIGILL, SIGFPE, SIGABRT};

// The open log file, for the crash handler, which can reach nothing else; null when there is none.
std::atomic<std::FILE*>& crashFile() {
    static std::atomic<std::FILE*> file{nullptr};
    return file;
}

// Writes `line` and a line break to `file` and flushes, so every line is on disk if the process dies next.
void writeLine(std::FILE* file, std::string_view line) {
    std::fwrite(line.data(), 1, line.size(), file);
    std::fputc('\n', file);
    std::fflush(file);
}

// The crash handler: dumps the ring into the log file (or stderr) and ends the process as the signal would have.
// Formatting allocates, which a signal handler should not: a best effort for a process that is dying anyway.
void onCrashSignal(int signal) {
    // NOLINTBEGIN(bugprone-signal-handler,cert-sig30-c): the dump allocates; see above.
    std::signal(signal, SIG_DFL);
    logging::detail::dumpOnFailure(signal == SIGABRT ? "abort" : "crash");
    if (std::FILE* file = crashFile().load(); file != nullptr) {
        std::fflush(file);
    }
    std::raise(signal);
    // NOLINTEND(bugprone-signal-handler,cert-sig30-c)
}

// Opens `path` for writing from the start, with the platform's wide paths on Windows (a user folder may not be
// ASCII).
std::FILE* openForWriting(const std::filesystem::path& path) {
#ifdef _WIN32
    std::FILE* file = nullptr;
    return _wfopen_s(&file, path.c_str(), L"wb") == 0 ? file : nullptr;
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

} // namespace

std::expected<std::unique_ptr<LogOutput>, Error> LogOutput::start(const std::optional<std::filesystem::path>& file,
                                                                  const logging::LogFilter& filter, bool keepPrevious) {
    std::unique_ptr<LogOutput> output(new LogOutput());
    // 1. The file, the previous run's kept beside it.
    if (file) {
        std::error_code ignored;
        if (keepPrevious && std::filesystem::exists(*file, ignored)) {
            std::filesystem::path previous = *file;
            previous.replace_extension(std::format(".prev{}", file->extension().string()));
            std::filesystem::rename(*file, previous, ignored);
        }
        output->m_file = openForWriting(*file);
        if (output->m_file == nullptr) {
            return fail(ErrorCode::Io, std::format("cannot write the log file {}", logPathText(*file)));
        }
        output->m_path = *file;
        crashFile() = output->m_file;
    }
    // 2. The sinks: the console by level, the file with the game's text withheld.
    std::FILE* logFile = output->m_file;
    logging::setFilter(filter);
    logging::setSink([logFile](const logging::Record& record, std::string_view line) {
        writeLine(record.level <= logging::Level::Warn ? stderr : stdout, line);
        if (logFile != nullptr) {
            writeLine(logFile, record.privacy == logging::Privacy::Public ? std::string(line)
                                                                          : logging::formatLine(withheld(record)));
        }
    });
    logging::setDumpSink([logFile](std::string_view line) { writeLine(logFile != nullptr ? logFile : stderr, line); });
    // 3. The crash handlers.
    for (const int signal : kCrashSignals) {
        std::signal(signal, onCrashSignal);
    }
    return output;
}

LogOutput::~LogOutput() {
    for (const int signal : kCrashSignals) {
        std::signal(signal, SIG_DFL);
    }
    logging::setSink({});
    logging::setDumpSink({});
    crashFile() = nullptr;
    if (m_file != nullptr) {
        std::fclose(m_file);
    }
}

std::string LogOutput::writeBugReport() {
    const std::size_t count = logging::dump("bug report");
    if (m_file != nullptr) {
        std::fflush(m_file);
    }
    return m_path ? std::format("bug report: the last {} events written to {}", count, logPathText(*m_path))
                  : std::format("bug report: the last {} events written to the console (no log file)", count);
}

std::filesystem::path defaultLogPath() {
    char* folder = SDL_GetPrefPath("Coney", "Coney");
    if (folder == nullptr) {
        return {};
    }
    std::filesystem::path path = std::filesystem::path(reinterpret_cast<const char8_t*>(folder)) / "coney.log";
    SDL_free(folder);
    return path;
}

std::string logPathText(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

} // namespace coney::platform
