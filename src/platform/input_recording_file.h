// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "core/error.h"
#include "core/input_recording.h"
#include "core/options.h"

namespace coney::platform {

/// The file a run's input recording (core/input_recording.h) is written to: the run log's companion, which a bug
/// report attaches and `--input-script` plays back (docs/guides/run-log.md#input-recording). Each write is flushed, so
/// a crash loses nothing. Coney's own debug tooling.
class InputRecordingFile {
  public:
    /// Opens `path` for writing from the start and writes `header` first. With `keepPrevious` an existing file is
    /// first renamed to `<name>.prev<ext>` (`coney.input.prev.txt`), so the run before a crash survives the next
    /// start. Fails with ErrorCode::Io when the file cannot be opened.
    [[nodiscard]] static std::expected<std::unique_ptr<InputRecordingFile>, Error>
    open(const std::filesystem::path& path, const InputRecordingHeader& header, bool keepPrevious);

    InputRecordingFile(const InputRecordingFile&) = delete;
    InputRecordingFile& operator=(const InputRecordingFile&) = delete;
    InputRecordingFile(InputRecordingFile&&) = delete;
    InputRecordingFile& operator=(InputRecordingFile&&) = delete;
    /// Closes the file.
    ~InputRecordingFile();

    /// Writes `lines` (whole lines, ending in a line break) and flushes.
    void write(std::string_view lines);

    /// The file's path.
    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

  private:
    InputRecordingFile() = default;

    std::filesystem::path m_path;
    std::FILE* m_file = nullptr;
};

/// Where a run writes its input recording, or nothing when it writes none: `--record-input FILE` when given; else,
/// for a run whose pads are a person's (`livePads`), beside the run log file `logFile` as `<its name>.input.txt`
/// (`coney.log` gives `coney.input.txt`). A run driven by a script or a driver program already has its input, so it
/// writes none unless `--record-input` asks.
[[nodiscard]] std::optional<std::filesystem::path>
inputRecordingPath(const Options& options, const std::optional<std::filesystem::path>& logFile, bool livePads);

/// A run's input recording at work: the file and the source writing into it, wrapped around the pads the game reads.
class InputRecording {
  public:
    InputRecording(const InputRecording&) = delete;
    InputRecording& operator=(const InputRecording&) = delete;
    InputRecording(InputRecording&&) = delete;
    InputRecording& operator=(InputRecording&&) = delete;
    ~InputRecording() = default;

    /// The pads to hand the game in place of `inner`: it records each step it is asked for.
    [[nodiscard]] InputSource& source() { return *m_input; }
    /// The file the recording goes to.
    [[nodiscard]] const std::filesystem::path& path() const { return m_file->path(); }

    /// Starts recording `inner` (which must outlive the result) when the run writes a recording
    /// (inputRecordingPath()); a null result when it does not. `started` is as RecordingInput's: a `--play-level` run
    /// begins at the level's first step of play. A file that cannot be opened is an error for `--record-input` and a
    /// warning in the log, with no recording, for the default one beside the log.
    [[nodiscard]] static std::expected<std::unique_ptr<InputRecording>, Error>
    start(const Options& options, const std::optional<std::filesystem::path>& logFile, bool livePads,
          const InputRecordingHeader& header, InputSource& inner, std::function<bool()> started);

  private:
    InputRecording() = default;

    std::unique_ptr<InputRecordingFile> m_file;
    std::unique_ptr<RecordingInput> m_input; // after the file: its last line goes into it as it ends
};

} // namespace coney::platform
