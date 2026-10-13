// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/clip_recorder.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

#include <SDL3/SDL.h>

namespace coney::platform {

namespace {

// The largest piece written to FFmpeg at once: SDL's pipe is non-blocking and takes what fits.
constexpr std::size_t kWriteChunk = std::size_t{64} * 1024;

// `args` as the null-terminated array of C strings SDL's process calls take; valid while `args` lives.
std::vector<const char*> argv(const std::vector<std::string>& args) {
    std::vector<const char*> result;
    result.reserve(args.size() + 1);
    for (const std::string& arg : args) {
        result.push_back(arg.c_str());
    }
    result.push_back(nullptr);
    return result;
}

// The FFmpeg to run: `CONEY_FFMPEG` when set (it must exist), else `ffmpeg`, which the system finds on PATH.
std::expected<std::string, Error> findFfmpeg() {
    const char* configured = SDL_getenv("CONEY_FFMPEG");
    if (configured == nullptr || *configured == '\0') {
        return std::string("ffmpeg");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(configured, error)) {
        return std::unexpected(
            Error{ErrorCode::NotFound, std::format("--record: CONEY_FFMPEG names no file ({})", configured)});
    }
    return std::string(configured);
}

// Folds `bytes` into the FNV-1a hash `hash`.
std::uint64_t fnv1a(std::uint64_t hash, std::span<const std::uint8_t> bytes) {
    for (const std::uint8_t byte : bytes) {
        hash = (hash ^ byte) * 0x100000001b3ULL;
    }
    return hash;
}

} // namespace

std::optional<std::filesystem::path> gitCheckoutAround(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path folder = std::filesystem::weakly_canonical(std::filesystem::absolute(path, error), error);
    while (true) {
        if (std::filesystem::exists(folder / ".git", error)) {
            return folder;
        }
        if (!folder.has_parent_path() || folder.parent_path() == folder) {
            return std::nullopt;
        }
        folder = folder.parent_path();
    }
}

std::expected<void, Error> checkClipLocation(const std::string& path) {
    const std::filesystem::path file{path};
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::absolute(file, error).parent_path();
    if (!std::filesystem::is_directory(folder, error)) {
        return std::unexpected(Error{ErrorCode::NotFound, std::format("--record: no folder {}", folder.string())});
    }
    if (const std::optional<std::filesystem::path> checkout = gitCheckoutAround(folder); checkout) {
        return std::unexpected(Error{ErrorCode::InvalidArgument,
                                     std::format("--record: {} is inside the git checkout {}; clips stay in the "
                                                 "scratch folder",
                                                 path, checkout->string())});
    }
    return {};
}

std::expected<std::unique_ptr<ClipRecorder>, Error> ClipRecorder::create(const std::string& path, int seconds,
                                                                         bool sound) {
    if (auto named = clips::checkClipPath(path); !named) {
        return std::unexpected(std::move(named.error()));
    }
    if (auto placed = checkClipLocation(path); !placed) {
        return std::unexpected(std::move(placed.error()));
    }
    auto ffmpeg = findFfmpeg();
    if (!ffmpeg) {
        return std::unexpected(std::move(ffmpeg.error()));
    }
    return std::unique_ptr<ClipRecorder>(new ClipRecorder(path, seconds, sound, std::move(*ffmpeg)));
}

ClipRecorder::ClipRecorder(std::string path, int seconds, bool sound, std::string ffmpeg)
    : m_path(std::move(path)), m_ffmpeg(std::move(ffmpeg)), m_seconds(std::clamp(seconds, 1, clips::kMaxClipSeconds)),
      m_sound(sound), m_frames(m_seconds) {}

ClipRecorder::~ClipRecorder() {
    // A recorder dropped without finish() (an error path): stop FFmpeg and leave no half-written clip behind.
    if (m_video != nullptr) {
        auto* process = static_cast<SDL_Process*>(m_video);
        SDL_KillProcess(process, true);
        SDL_WaitProcess(process, true, nullptr);
        SDL_DestroyProcess(process);
        m_video = nullptr;
        removeParts(true);
    }
}

bool ClipRecorder::startVideo(clips::ClipSize size) {
    m_source = size;
    const std::string output = m_sound ? clips::partPath(m_path, ".video.part.mp4") : m_path;
    const std::vector<std::string> args = clips::videoEncodeArguments({m_ffmpeg, size, output, m_seconds});
    std::vector<const char*> pointers = argv(args);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, static_cast<void*>(pointers.data()));
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (process == nullptr) {
        m_failure = Error{ErrorCode::PlatformFailure,
                          std::format("--record: could not start {} ({}); install FFmpeg or set CONEY_FFMPEG", m_ffmpeg,
                                      SDL_GetError())};
        return false;
    }
    m_video = process;
    return true;
}

bool ClipRecorder::writeToVideo(std::span<const std::uint8_t> bytes) {
    auto* process = static_cast<SDL_Process*>(m_video);
    SDL_IOStream* input = SDL_GetProcessInput(process);
    if (input == nullptr) {
        return false;
    }
    while (!bytes.empty()) {
        const std::size_t written = SDL_WriteIO(input, bytes.data(), std::min(bytes.size(), kWriteChunk));
        if (written == 0) {
            // A full pipe waits for FFmpeg to read; an error, or FFmpeg gone, ends the clip.
            int exitCode = 0;
            if (SDL_GetIOStatus(input) == SDL_IO_STATUS_ERROR || SDL_WaitProcess(process, false, &exitCode)) {
                return false;
            }
            SDL_Delay(1);
            continue;
        }
        bytes = bytes.subspan(written);
    }
    return true;
}

void ClipRecorder::addFrame(std::span<const std::uint8_t> rgba, clips::ClipSize size) {
    m_soundDue = false;
    if (!recording()) {
        return;
    }
    if (m_video == nullptr && !startVideo(size)) {
        return;
    }
    if (size != m_source || rgba.size() != static_cast<std::size_t>(size.width) * size.height * 4) {
        m_failure = Error{ErrorCode::Invalid, "--record: the frame size changed while recording"};
        return;
    }
    if (!writeToVideo(rgba)) {
        m_failure = Error{ErrorCode::PlatformFailure, "--record: FFmpeg stopped taking frames (see its message)"};
        return;
    }
    m_hash = fnv1a(m_hash, rgba);
    m_frames.take();
    m_soundDue = true;
}

void ClipRecorder::addSound(std::span<const std::int16_t> samples) {
    if (m_sound && m_soundDue) {
        m_samples.insert(m_samples.end(), samples.begin(), samples.end());
    }
    m_soundDue = false;
}

bool ClipRecorder::waitAndDestroy(void* process) {
    auto* sdlProcess = static_cast<SDL_Process*>(process);
    int exitCode = -1;
    const bool ended = SDL_WaitProcess(sdlProcess, true, &exitCode);
    SDL_DestroyProcess(sdlProcess);
    return ended && exitCode == 0;
}

void ClipRecorder::removeParts(bool clipToo) const {
    std::error_code ignored;
    std::filesystem::remove(clips::partPath(m_path, ".video.part.mp4"), ignored);
    std::filesystem::remove(clips::partPath(m_path, ".audio.part.raw"), ignored);
    if (clipToo) {
        std::filesystem::remove(m_path, ignored);
    }
}

std::expected<ClipSummary, Error> ClipRecorder::finish() {
    if (m_finished) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "--record: the clip was already finished"});
    }
    m_finished = true;
    if (m_video == nullptr) {
        return std::unexpected(m_failure.value_or(Error{ErrorCode::Invalid, "--record: no frame was recorded"}));
    }
    // Closing FFmpeg's input ends its stream; it then writes the file and exits.
    auto* process = static_cast<SDL_Process*>(m_video);
    SDL_CloseIO(SDL_GetProcessInput(process));
    const bool encoded = waitAndDestroy(m_video);
    m_video = nullptr;
    if (m_failure || !encoded) {
        removeParts(true);
        return std::unexpected(m_failure.value_or(Error{ErrorCode::PlatformFailure, "--record: FFmpeg failed"}));
    }

    // The sound pass: the steps' samples as a raw file beside the clip, muxed with the video.
    const bool sound = m_sound && !m_samples.empty();
    if (m_sound && !sound) {
        // Nothing was heard (no sound output in this run): the video pass's file is the clip.
        std::error_code renamed;
        std::filesystem::rename(clips::partPath(m_path, ".video.part.mp4"), m_path, renamed);
        if (renamed) {
            removeParts(true);
            return std::unexpected(Error{ErrorCode::Io, "--record: could not move the video into place"});
        }
    } else if (sound) {
        const std::string audio = clips::partPath(m_path, ".audio.part.raw");
        bool muxed = false;
        if (std::ofstream file(audio, std::ios::binary | std::ios::trunc); file) {
            file.write(reinterpret_cast<const char*>(m_samples.data()),
                       static_cast<std::streamsize>(m_samples.size() * sizeof(std::int16_t)));
            file.close();
            muxed = !file.fail();
        }
        if (muxed) {
            const std::vector<std::string> args =
                clips::muxArguments(m_ffmpeg, clips::partPath(m_path, ".video.part.mp4"), audio, m_path, m_seconds);
            std::vector<const char*> pointers = argv(args);
            SDL_Process* mux = SDL_CreateProcess(pointers.data(), false);
            muxed = mux != nullptr && waitAndDestroy(mux);
        }
        removeParts(!muxed);
        if (!muxed) {
            return std::unexpected(Error{ErrorCode::PlatformFailure, "--record: muxing the sound failed"});
        }
    }

    std::error_code error;
    const std::uint64_t bytes = std::filesystem::file_size(m_path, error);
    return ClipSummary{m_path, m_frames.taken(),  m_source, clips::clipOutputSize(m_source),
                       sound,  error ? 0 : bytes, m_hash};
}

} // namespace coney::platform
