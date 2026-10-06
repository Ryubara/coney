// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/ffmpeg_info.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace coney::platform {

FfmpegInfo ffmpegInfo() {
    FfmpegInfo info;
    info.version = av_version_info();
    info.license = avcodec_license();
    info.binkDemuxer = av_find_input_format("bink") != nullptr;
    info.binkVideo = avcodec_find_decoder(AV_CODEC_ID_BINKVIDEO) != nullptr;
    info.binkAudio = avcodec_find_decoder(AV_CODEC_ID_BINKAUDIO_DCT) != nullptr;
    return info;
}

} // namespace coney::platform
