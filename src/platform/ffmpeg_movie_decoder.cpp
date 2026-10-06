// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/ffmpeg_movie_decoder.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace coney::platform {

namespace {

// The size of the buffer FFmpeg reads the file through.
constexpr int kIoBufferSize = 64 * 1024;

// FFmpeg's message for `code`.
std::string ffmpegError(int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(code, text.data(), text.size());
    return std::string(text.data());
}

// Frees each FFmpeg object with its own function.
struct FormatDeleter {
    void operator()(AVFormatContext* context) const { avformat_close_input(&context); }
};
struct CodecDeleter {
    void operator()(AVCodecContext* context) const { avcodec_free_context(&context); }
};
struct IoDeleter {
    void operator()(AVIOContext* context) const {
        if (context != nullptr) {
            av_freep(static_cast<void*>(&context->buffer));
        }
        avio_context_free(&context);
    }
};
struct PacketDeleter {
    void operator()(AVPacket* packet) const { av_packet_free(&packet); }
};
struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

// `value` clamped to a byte.
std::uint8_t clampByte(int value) { return static_cast<std::uint8_t>(std::clamp(value, 0, 255)); }

// Converts a YUV 4:2:0 picture to RGBA, BT.601 studio range in 16.16 fixed point.
void yuv420ToRgba(const AVFrame& frame, std::span<std::uint8_t> rgba, int width, int height) {
    constexpr int kY = 76309;   // 1.164 × 65536
    constexpr int kRv = 104597; // 1.596
    constexpr int kGu = 25675;  // 0.391
    constexpr int kGv = 53279;  // 0.813
    constexpr int kBu = 132201; // 2.018
    constexpr int kHalf = 32768;
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* lumaRow = frame.data[0] + static_cast<std::ptrdiff_t>(y) * frame.linesize[0];
        const std::uint8_t* uRow = frame.data[1] + static_cast<std::ptrdiff_t>(y / 2) * frame.linesize[1];
        const std::uint8_t* vRow = frame.data[2] + static_cast<std::ptrdiff_t>(y / 2) * frame.linesize[2];
        std::uint8_t* out = rgba.data() + static_cast<std::ptrdiff_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            const int luma = (lumaRow[x] - 16) * kY;
            const int u = uRow[x / 2] - 128;
            const int v = vRow[x / 2] - 128;
            out[0] = clampByte((luma + kRv * v + kHalf) >> 16);
            out[1] = clampByte((luma - kGu * u - kGv * v + kHalf) >> 16);
            out[2] = clampByte((luma + kBu * u + kHalf) >> 16);
            out[3] = 255;
            out += 4;
        }
    }
}

// A 16-bit sample from a float one, -1 to 1.
std::int16_t toPcm16(float sample) {
    const float scaled = std::clamp(sample, -1.0F, 1.0F) * 32767.0F;
    return static_cast<std::int16_t>(std::lround(scaled));
}

// The Bink movie decoder over FFmpeg.
class BinkMovieDecoder final : public movies::MovieDecoder {
  public:
    // Opens `file`; see openBinkMovie().
    static std::expected<std::unique_ptr<movies::MovieDecoder>, Error> open(std::unique_ptr<io::Stream> file) {
        std::unique_ptr<BinkMovieDecoder> decoder(new BinkMovieDecoder(std::move(file)));
        if (auto opened = decoder->openFile(); !opened) {
            return std::unexpected(std::move(opened.error()));
        }
        return std::unique_ptr<movies::MovieDecoder>(std::move(decoder));
    }

    [[nodiscard]] const movies::MovieInfo& info() const override { return m_info; }

    std::expected<bool, Error> decodeFrame(std::span<std::uint8_t> rgba) override {
        while (true) {
            // A frame the video decoder already holds.
            const int received = avcodec_receive_frame(m_video.get(), m_frame.get());
            if (received == 0) {
                if (!rgba.empty()) {
                    convert(rgba);
                }
                av_frame_unref(m_frame.get());
                return true;
            }
            if (received == AVERROR_EOF) {
                return false;
            }
            if (received != AVERROR(EAGAIN)) {
                return fail(ErrorCode::Invalid, std::format("Bink video: {}", ffmpegError(received)));
            }
            if (m_ended) {
                return false;
            }
            // Read on: sound packets are decoded on the way, the next video packet goes to the decoder.
            const int read = av_read_frame(m_format.get(), m_packet.get());
            if (read < 0) {
                m_ended = true;
                avcodec_send_packet(m_video.get(), nullptr); // drain
                if (read != AVERROR_EOF && m_readError) {
                    return fail(ErrorCode::Io, "I/O error reading the Bink file");
                }
                continue;
            }
            if (m_packet->stream_index == m_videoStream) {
                const int sent = avcodec_send_packet(m_video.get(), m_packet.get());
                av_packet_unref(m_packet.get());
                if (sent < 0) {
                    return fail(ErrorCode::Invalid, std::format("Bink video: {}", ffmpegError(sent)));
                }
            } else if (m_packet->stream_index == m_audioStream && m_audio) {
                const int sent = avcodec_send_packet(m_audio.get(), m_packet.get());
                av_packet_unref(m_packet.get());
                if (sent < 0) {
                    return fail(ErrorCode::Invalid, std::format("Bink audio: {}", ffmpegError(sent)));
                }
                if (auto drained = drainAudio(); !drained) {
                    return std::unexpected(std::move(drained.error()));
                }
            } else {
                av_packet_unref(m_packet.get());
            }
        }
    }

    std::size_t takeAudio(std::vector<std::int16_t>& out) override {
        const std::size_t count = m_pcm.size();
        out.insert(out.end(), m_pcm.begin(), m_pcm.end());
        m_pcm.clear();
        return count;
    }

  private:
    explicit BinkMovieDecoder(std::unique_ptr<io::Stream> file) : m_file(std::move(file)) {}

    // FFmpeg's read callback: up to `size` bytes from the file; AVERROR_EOF at its end.
    static int readPacket(void* opaque, std::uint8_t* buffer, int size) {
        auto* self = static_cast<BinkMovieDecoder*>(opaque);
        const std::uint64_t left = self->m_file->remaining();
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(left, static_cast<std::uint64_t>(size)));
        if (count == 0) {
            return AVERROR_EOF;
        }
        if (auto done = self->m_file->read(std::span(reinterpret_cast<std::byte*>(buffer), count)); !done) {
            self->m_readError = true;
            return AVERROR(EIO);
        }
        return static_cast<int>(count);
    }

    // FFmpeg's seek callback; AVSEEK_SIZE asks for the file's size.
    static std::int64_t seekFile(void* opaque, std::int64_t offset, int whence) {
        auto* self = static_cast<BinkMovieDecoder*>(opaque);
        const auto size = static_cast<std::int64_t>(self->m_file->size());
        if ((whence & AVSEEK_SIZE) != 0) {
            return size;
        }
        std::int64_t target = offset;
        switch (whence & ~AVSEEK_FORCE) {
        case SEEK_SET:
            break;
        case SEEK_CUR:
            target += static_cast<std::int64_t>(self->m_file->tell());
            break;
        case SEEK_END:
            target += size;
            break;
        default:
            return -1;
        }
        if (target < 0 || target > size || !self->m_file->seek(static_cast<std::uint64_t>(target))) {
            return -1;
        }
        return target;
    }

    // Opens the demuxer over the file and the decoders of its video track and first sound track.
    std::expected<void, Error> openFile() {
        auto* buffer = static_cast<std::uint8_t*>(av_malloc(kIoBufferSize));
        if (buffer == nullptr) {
            return fail(ErrorCode::PlatformFailure, "FFmpeg could not allocate its read buffer");
        }
        m_io.reset(avio_alloc_context(buffer, kIoBufferSize, 0, this, &readPacket, nullptr, &seekFile));
        if (!m_io) {
            av_free(buffer);
            return fail(ErrorCode::PlatformFailure, "FFmpeg could not make its reader");
        }
        AVFormatContext* format = avformat_alloc_context();
        if (format == nullptr) {
            return fail(ErrorCode::PlatformFailure, "FFmpeg could not make a demuxer");
        }
        format->pb = m_io.get();
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        // On failure avformat_open_input frees the context itself.
        if (const int opened = avformat_open_input(&format, nullptr, av_find_input_format("bink"), nullptr);
            opened < 0) {
            return fail(ErrorCode::Invalid, std::format("not a Bink movie FFmpeg can read ({})", ffmpegError(opened)));
        }
        m_format.reset(format);

        for (unsigned i = 0; i < m_format->nb_streams; ++i) {
            const AVCodecParameters* parameters = m_format->streams[i]->codecpar;
            if (parameters->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStream < 0) {
                m_videoStream = static_cast<int>(i);
            } else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStream < 0) {
                m_audioStream = static_cast<int>(i);
            }
        }
        if (m_videoStream < 0) {
            return fail(ErrorCode::Invalid, "the Bink movie has no video track");
        }
        auto video = openCodec(m_format->streams[m_videoStream]->codecpar);
        if (!video) {
            return std::unexpected(std::move(video.error()));
        }
        m_video = std::move(*video);
        if (m_audioStream >= 0) {
            auto audio = openCodec(m_format->streams[m_audioStream]->codecpar);
            if (!audio) {
                return std::unexpected(std::move(audio.error()));
            }
            m_audio = std::move(*audio);
        }
        m_packet.reset(av_packet_alloc());
        m_frame.reset(av_frame_alloc());
        m_audioFrame.reset(av_frame_alloc());
        if (!m_packet || !m_frame || !m_audioFrame) {
            return fail(ErrorCode::PlatformFailure, "FFmpeg could not allocate a frame");
        }

        // The header's values.
        const AVStream* stream = m_format->streams[m_videoStream];
        m_info.width = stream->codecpar->width;
        m_info.height = stream->codecpar->height;
        m_info.rateNumerator = static_cast<std::uint32_t>(stream->avg_frame_rate.num);
        m_info.rateDenominator = static_cast<std::uint32_t>(stream->avg_frame_rate.den);
        m_info.frameCount = static_cast<std::uint32_t>(std::max<std::int64_t>(stream->duration, 0));
        if (m_audio) {
            m_info.hasAudio = true;
            m_info.sampleRate = m_audio->sample_rate;
            m_info.channels = m_audio->ch_layout.nb_channels;
        }
        return {};
    }

    // A decoder for `parameters`, on one thread: the movie is decoded on the game thread, a frame a step.
    static std::expected<std::unique_ptr<AVCodecContext, CodecDeleter>, Error>
    openCodec(const AVCodecParameters* parameters) {
        const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
        if (codec == nullptr) {
            return fail(ErrorCode::Invalid, std::format("no decoder for {}", avcodec_get_name(parameters->codec_id)));
        }
        std::unique_ptr<AVCodecContext, CodecDeleter> context(avcodec_alloc_context3(codec));
        if (!context || avcodec_parameters_to_context(context.get(), parameters) < 0) {
            return fail(ErrorCode::PlatformFailure, "FFmpeg could not make a decoder");
        }
        context->thread_count = 1;
        if (const int opened = avcodec_open2(context.get(), codec, nullptr); opened < 0) {
            return fail(ErrorCode::Invalid,
                        std::format("{} decoder: {}", avcodec_get_name(parameters->codec_id), ffmpegError(opened)));
        }
        return context;
    }

    // Moves every frame the sound decoder holds into the PCM buffer, interleaved 16-bit.
    std::expected<void, Error> drainAudio() {
        while (true) {
            const int received = avcodec_receive_frame(m_audio.get(), m_audioFrame.get());
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                return {};
            }
            if (received < 0) {
                return fail(ErrorCode::Invalid, std::format("Bink audio: {}", ffmpegError(received)));
            }
            const AVFrame& frame = *m_audioFrame;
            const int channels = frame.ch_layout.nb_channels;
            const auto format = static_cast<AVSampleFormat>(frame.format);
            for (int i = 0; i < frame.nb_samples; ++i) {
                for (int c = 0; c < channels; ++c) {
                    float sample = 0.0F;
                    if (format == AV_SAMPLE_FMT_FLTP) {
                        sample = reinterpret_cast<const float*>(frame.extended_data[c])[i];
                    } else if (format == AV_SAMPLE_FMT_FLT) {
                        sample = reinterpret_cast<const float*>(frame.extended_data[0])[(i * channels) + c];
                    }
                    m_pcm.push_back(toPcm16(sample));
                }
            }
            av_frame_unref(m_audioFrame.get());
        }
    }

    // Writes the decoded picture into `rgba`, the header's size.
    void convert(std::span<std::uint8_t> rgba) const {
        const int width = std::min(m_frame->width, m_info.width);
        const int height = std::min(m_frame->height, m_info.height);
        if (rgba.size() < static_cast<std::size_t>(m_info.width) * m_info.height * 4) {
            return;
        }
        yuv420ToRgba(*m_frame, rgba, width, height);
    }

    std::unique_ptr<io::Stream> m_file;
    bool m_readError = false;
    bool m_ended = false;
    // Declared in the order they must be freed in reverse: the demuxer reads through the reader.
    std::unique_ptr<AVIOContext, IoDeleter> m_io;
    std::unique_ptr<AVFormatContext, FormatDeleter> m_format;
    std::unique_ptr<AVCodecContext, CodecDeleter> m_video;
    std::unique_ptr<AVCodecContext, CodecDeleter> m_audio;
    std::unique_ptr<AVPacket, PacketDeleter> m_packet;
    std::unique_ptr<AVFrame, FrameDeleter> m_frame;
    std::unique_ptr<AVFrame, FrameDeleter> m_audioFrame;
    int m_videoStream = -1;
    int m_audioStream = -1;
    movies::MovieInfo m_info;
    std::vector<std::int16_t> m_pcm;
};

} // namespace

std::expected<std::unique_ptr<movies::MovieDecoder>, Error> openBinkMovie(std::unique_ptr<io::Stream> file) {
    return BinkMovieDecoder::open(std::move(file));
}

movies::MovieOpener discMovieOpener(const io::Disc& disc) {
    return [&disc](std::string_view name) -> std::expected<std::unique_ptr<movies::MovieDecoder>, Error> {
        const std::string path = std::format("PSS/{}.BIK", name);
        auto file = disc.openFile(path);
        if (!file) {
            return std::unexpected(std::move(file.error()));
        }
        return openBinkMovie(std::make_unique<io::FileStream>(std::move(*file)));
    };
}

} // namespace coney::platform
