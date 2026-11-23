// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/input_recording_file.h"

#include <format>
#include <string>
#include <system_error>
#include <utility>

#include "core/log.h"
#include "platform/log_output.h"

namespace coney::platform {

namespace {

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

std::expected<std::unique_ptr<InputRecordingFile>, Error>
InputRecordingFile::open(const std::filesystem::path& path, const InputRecordingHeader& header, bool keepPrevious) {
    std::unique_ptr<InputRecordingFile> file(new InputRecordingFile());
    // The previous run's recording kept beside this one, as the run log keeps its own.
    std::error_code ignored;
    if (keepPrevious && std::filesystem::exists(path, ignored)) {
        std::filesystem::path previous = path;
        previous.replace_extension(std::format(".prev{}", path.extension().string()));
        std::filesystem::rename(path, previous, ignored);
    }
    file->m_file = openForWriting(path);
    if (file->m_file == nullptr) {
        return fail(ErrorCode::Io, std::format("cannot write the input recording {}", logPathText(path.filename())));
    }
    file->m_path = path;
    file->write(inputRecordingHeader(header));
    return file;
}

InputRecordingFile::~InputRecordingFile() {
    if (m_file != nullptr) {
        std::fclose(m_file);
    }
}

void InputRecordingFile::write(std::string_view lines) {
    if (m_file == nullptr) {
        return;
    }
    std::fwrite(lines.data(), 1, lines.size(), m_file);
    std::fflush(m_file);
}

std::optional<std::filesystem::path>
inputRecordingPath(const Options& options, const std::optional<std::filesystem::path>& logFile, bool livePads) {
    if (const std::optional<std::string> given = options.recordInput; given) {
        return std::filesystem::path(*given);
    }
    if (!livePads || !logFile) {
        return std::nullopt;
    }
    return logFile->parent_path() / (logFile->stem().string() + ".input.txt");
}

std::expected<std::unique_ptr<InputRecording>, Error>
InputRecording::start(const Options& options, const std::optional<std::filesystem::path>& logFile, bool livePads,
                      const InputRecordingHeader& header, InputSource& inner, std::function<bool()> started) {
    const std::optional<std::filesystem::path> path = inputRecordingPath(options, logFile, livePads);
    if (!path) {
        return std::unique_ptr<InputRecording>();
    }
    // The default file's predecessor is kept, as the log's is; one named with `--record-input` or beside a
    // `--log-file` is overwritten.
    const bool keepPrevious = !options.recordInput.has_value() && !options.logFile.has_value();
    auto file = InputRecordingFile::open(*path, header, keepPrevious);
    if (!file) {
        if (options.recordInput) {
            return std::unexpected(std::move(file.error()));
        }
        logging::warn(logging::Category::Engine, std::format("coney: {}; no input recording", file.error().message));
        return std::unique_ptr<InputRecording>();
    }
    std::unique_ptr<InputRecording> recording(new InputRecording());
    recording->m_file = std::move(*file);
    InputRecordingFile* target = recording->m_file.get();
    recording->m_input = std::make_unique<RecordingInput>(
        inner, [target](std::string_view lines) { target->write(lines); }, std::move(started));
    return recording;
}

} // namespace coney::platform
