// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/clip_recording.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <utility>

namespace coney::clips {

namespace {

// The arguments every pass ends with: no metadata or chapters copied, no version strings in the headers, the index
// at the front so the clip plays while it downloads, the length cut at `seconds`, then the output.
void appendOutputRules(std::vector<std::string>& args, int seconds, const std::string& output) {
    for (const char* arg :
         {"-map_metadata", "-1", "-map_chapters", "-1", "-fflags", "+bitexact", "-movflags", "+faststart", "-t"}) {
        args.emplace_back(arg);
    }
    args.push_back(std::to_string(seconds));
    args.push_back(output);
}

// Whether `c` may appear in a clip's file name.
bool isNameCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-';
}

} // namespace

ClipFrames::ClipFrames(int seconds) : m_limit(std::clamp(seconds, 1, kMaxClipSeconds) * kClipFps) {}

bool ClipFrames::take() {
    if (full()) {
        return false;
    }
    ++m_taken;
    return true;
}

ClipSize clipOutputSize(ClipSize source) {
    if (source.width <= 0 || source.height <= 0) {
        return {960, kClipHeight};
    }
    const double width = static_cast<double>(kClipHeight) * source.width / source.height;
    const int even = static_cast<int>(std::lround(width / 2.0)) * 2;
    return {std::max(even, 2), kClipHeight};
}

std::expected<void, Error> checkClipPath(std::string_view path) {
    const std::filesystem::path file{std::string(path)};
    const std::string name = file.filename().string();
    const bool plain =
        name.size() > 4 && name.ends_with(".mp4") && name.front() != '.' && std::ranges::all_of(name, isNameCharacter);
    if (!plain) {
        return std::unexpected(Error{ErrorCode::InvalidArgument,
                                     std::format("--record: \"{}\" is not a plain clip name (letters, digits, '.', "
                                                 "'_' and '-', ending in .mp4)",
                                                 name)});
    }
    if (file.parent_path().filename() != kClipFolder) {
        return std::unexpected(
            Error{ErrorCode::InvalidArgument, std::format("--record: a clip goes in a scratch folder's {}/ folder "
                                                          "(<scratch>/<task>/{}/{})",
                                                          kClipFolder, kClipFolder, name)});
    }
    return {};
}

std::vector<std::string> videoEncodeArguments(const VideoEncode& encode) {
    const ClipSize out = clipOutputSize(encode.source);
    std::vector<std::string> args{encode.ffmpeg,
                                  "-hide_banner",
                                  "-loglevel",
                                  "error",
                                  "-y",
                                  "-f",
                                  "rawvideo",
                                  "-pixel_format",
                                  "rgba",
                                  "-video_size",
                                  std::format("{}x{}", encode.source.width, encode.source.height),
                                  "-framerate",
                                  std::to_string(kClipFps),
                                  "-i",
                                  "pipe:0",
                                  "-an",
                                  "-vf",
                                  std::format("scale={}:{}:flags=lanczos,format=yuv420p", out.width, out.height),
                                  "-c:v",
                                  "libx264",
                                  "-preset",
                                  "slow",
                                  "-crf",
                                  "26",
                                  "-bsf:v",
                                  "filter_units=remove_types=6",
                                  "-flags:v",
                                  "+bitexact"};
    appendOutputRules(args, encode.seconds, encode.output);
    return args;
}

std::vector<std::string> muxArguments(const std::string& ffmpeg, const std::string& video, const std::string& audio,
                                      const std::string& output, int seconds) {
    std::vector<std::string> args{ffmpeg,
                                  "-hide_banner",
                                  "-loglevel",
                                  "error",
                                  "-y",
                                  "-i",
                                  video,
                                  "-f",
                                  "s16le",
                                  "-ar",
                                  std::to_string(kClipAudioRate),
                                  "-ac",
                                  "2",
                                  "-i",
                                  audio,
                                  "-map",
                                  "0:v:0",
                                  "-map",
                                  "1:a:0",
                                  "-c:v",
                                  "copy",
                                  "-c:a",
                                  "aac",
                                  "-b:a",
                                  "96k",
                                  "-flags:a",
                                  "+bitexact",
                                  "-shortest"};
    appendOutputRules(args, seconds, output);
    return args;
}

std::string partPath(const std::string& output, std::string_view suffix) {
    std::filesystem::path path{output};
    const std::string stem = path.stem().string();
    return (path.parent_path() / (stem + std::string(suffix))).generic_string();
}

} // namespace coney::clips
