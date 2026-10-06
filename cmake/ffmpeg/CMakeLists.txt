# SPDX-License-Identifier: GPL-3.0-or-later
# A trimmed FFmpeg for the game's Bink movies (docs/research/movies.md): the Bink demuxer, the Bink video decoder and
# the Bink audio (DCT) decoder with the parts of libavformat, libavcodec and libavutil they need, as one static library,
# `ffmpeg_bink`. FFmpeg itself is LGPL-2.1-or-later; nothing here enables a GPL, version-3 or nonfree part.
#
# FFmpeg builds with its own configure script and make, which need a POSIX shell (MSYS2 on Windows) and reject a source
# or build path with a space in it. So instead of running configure, this file compiles the few dozen FFmpeg sources
# the three components need with CMake, on every platform and compiler Coney supports, and writes the files configure
# would have generated:
#
# - `config.h`: every CONFIG_*, HAVE_* and ARCH_* name the sources use is defined, to 0 unless set below. The names
#   are collected from the sources themselves (FFmpeg tests them with `#if NAME` or `if (NAME)`, never `#ifdef`), so
#   a version update needs no new list. No ARCH_* is set: FFmpeg's assembly and intrinsics stay out (the plain C code
#   decodes a 640 x 448 movie many times faster than real time).
# - `config_components.h`, `libavutil/avconfig.h`, `libavutil/ffversion.h`, and the component lists configure writes
#   (`codec_list.c`, `demuxer_list.c`, ...), which name exactly the three components.
#
# The source list is what a configure build with the same components links into a program that opens a Bink file,
# decodes every frame and every audio packet (checked with the linker's map on Linux), plus the three version.c files
# for avcodec_license() and friends. That configure build is:
#   ./configure --disable-everything --disable-programs --disable-doc --disable-network --disable-avdevice
#     --disable-avfilter --disable-swscale --disable-swresample --disable-asm --disable-autodetect --disable-iconv
#     --enable-pthreads --enable-demuxer=bink --enable-decoder=bink,binkaudio_dct
# How to update FFmpeg: docs/guides/building.md#ffmpeg.

# Called from cmake/deps.cmake with FFMPEG_SOURCE_DIR set to the fetched sources.
set(src "${FFMPEG_SOURCE_DIR}")
set(gen "${CMAKE_CURRENT_BINARY_DIR}/generated")

set(ffmpeg_sources
    libavcodec/allcodecs.c libavcodec/avcodec.c libavcodec/bink.c libavcodec/binkaudio.c libavcodec/binkdsp.c
    libavcodec/bitstream_filters.c libavcodec/blockdsp.c libavcodec/bsf.c libavcodec/codec_desc.c
    libavcodec/codec_par.c libavcodec/decode.c libavcodec/encode.c libavcodec/exif.c libavcodec/get_buffer.c
    libavcodec/hpeldsp.c libavcodec/options.c libavcodec/packet.c libavcodec/parser.c libavcodec/parsers.c
    libavcodec/profiles.c libavcodec/pthread.c libavcodec/pthread_frame.c libavcodec/pthread_slice.c libavcodec/raw.c
    libavcodec/threadprogress.c libavcodec/tiff_common.c libavcodec/to_upper4.c libavcodec/utils.c
    libavcodec/version.c libavcodec/vlc.c libavcodec/wma_freqs.c
    libavformat/allformats.c libavformat/avformat.c libavformat/avio.c libavformat/aviobuf.c libavformat/bink.c
    libavformat/demux.c libavformat/demux_utils.c libavformat/format.c libavformat/id3v1.c libavformat/id3v2.c
    libavformat/metadata.c libavformat/options.c libavformat/packet_list.c libavformat/protocols.c
    libavformat/seek.c libavformat/url.c libavformat/urldecode.c libavformat/utils.c libavformat/version.c
    libavutil/avsscanf.c libavutil/avstring.c libavutil/bprint.c libavutil/buffer.c libavutil/channel_layout.c
    libavutil/container_fifo.c libavutil/cpu.c libavutil/crc.c libavutil/dict.c libavutil/display.c libavutil/error.c
    libavutil/eval.c libavutil/fifo.c libavutil/file_open.c libavutil/frame.c libavutil/hwcontext.c libavutil/iamf.c
    libavutil/imgutils.c libavutil/log.c libavutil/log2_tab.c libavutil/mastering_display_metadata.c
    libavutil/mathematics.c libavutil/mem.c libavutil/opt.c libavutil/parseutils.c libavutil/pixdesc.c
    libavutil/random_seed.c libavutil/rational.c libavutil/refstruct.c libavutil/reverse.c libavutil/samplefmt.c
    libavutil/sha.c libavutil/side_data.c libavutil/slicethread.c libavutil/time.c libavutil/timecode_internal.c
    libavutil/timestamp.c libavutil/tx.c libavutil/tx_double.c libavutil/tx_float.c libavutil/tx_int32.c
    libavutil/utils.c libavutil/version.c)
list(TRANSFORM ffmpeg_sources PREPEND "${src}/")

# The names set to something other than 0. Everything else the sources test is 0.
set(ffmpeg_on
    # The three components and what they pull in (configure's dependency resolution for them).
    CONFIG_BINK_DECODER CONFIG_BINKAUDIO_DCT_DECODER CONFIG_BINK_DEMUXER CONFIG_DECODERS CONFIG_DEMUXERS
    CONFIG_AVCODEC CONFIG_AVFORMAT CONFIG_AVUTIL CONFIG_BLOCKDSP CONFIG_HPELDSP CONFIG_WMA_FREQS CONFIG_IAMF
    CONFIG_STATIC CONFIG_SAFE_BITSTREAM_READER CONFIG_RUNTIME_CPUDETECT
    # Threads (FFmpeg's internal once-only initialisation needs them; Coney decodes with one thread).
    HAVE_THREADS
    # C99 maths, which every supported compiler's library has.
    HAVE_ATANF HAVE_ATAN2F HAVE_CBRT HAVE_CBRTF HAVE_COPYSIGN HAVE_COSF HAVE_ERF HAVE_EXP2 HAVE_EXP2F HAVE_EXPF HAVE_HYPOT
    HAVE_ISFINITE HAVE_ISINF HAVE_ISNAN HAVE_LDEXPF HAVE_LLRINT HAVE_LLRINTF HAVE_LOG2 HAVE_LOG2F HAVE_LOG10F HAVE_LRINT
    HAVE_LRINTF HAVE_POWF HAVE_RINT HAVE_ROUND HAVE_ROUNDF HAVE_SINF HAVE_TRUNC HAVE_TRUNCF HAVE_GETENV)
if(WIN32)
    list(APPEND ffmpeg_on HAVE_W32THREADS HAVE_WINDOWS_H HAVE_IO_H HAVE_DIRECT_H HAVE_DOS_PATHS HAVE_ALIGNED_MALLOC
         HAVE_GETSYSTEMTIMEASFILETIME HAVE_GETPROCESSAFFINITYMASK HAVE_SETCONSOLETEXTATTRIBUTE HAVE_GETSTDHANDLE
         HAVE_SLEEP)
else()
    list(APPEND ffmpeg_on HAVE_PTHREADS HAVE_UNISTD_H HAVE_SYS_TIME_H HAVE_SYS_PARAM_H HAVE_DIRENT_H HAVE_ACCESS
         HAVE_CLOCK_GETTIME HAVE_GETTIMEOFDAY HAVE_NANOSLEEP HAVE_USLEEP HAVE_ISATTY HAVE_POSIX_MEMALIGN HAVE_SYSCONF
         HAVE_FCNTL HAVE_MKSTEMP HAVE_GMTIME_R HAVE_LOCALTIME_R HAVE_STRERROR_R HAVE_LSTAT)
endif()

# Writes `content` to `path` only when it differs from what is there, so a re-run of CMake does not rebuild FFmpeg.
function(ffmpeg_write path content)
    if(EXISTS "${path}")
        file(READ "${path}" old)
        if(old STREQUAL content)
            return()
        endif()
    endif()
    file(WRITE "${path}" "${content}")
endfunction()

# config.h. The scan reads the compiled sources and every header of the three libraries (about 9 MB), so its result
# is kept and redone only when the FFmpeg sources or this file change.
set(names_file "${gen}/config-names.txt")
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" list_hash)
set(scan_key "${FFMPEG_SOURCE_DIR}|${list_hash}")
set(cached_key "")
if(EXISTS "${gen}/config-names.key")
    file(READ "${gen}/config-names.key" cached_key)
endif()
if(NOT cached_key STREQUAL scan_key OR NOT EXISTS "${names_file}")
    # The headers, and the templates sources include as .c files (tx_template.c, hpel_template.c).
    file(GLOB headers "${src}/libavutil/*.h" "${src}/libavcodec/*.h" "${src}/libavformat/*.h" "${src}/compat/*.h"
         "${src}/libavutil/*template.c" "${src}/libavcodec/*template.c")
    set(names "")
    set(defined_in_source "")
    foreach(file IN LISTS ffmpeg_sources headers)
        # Only the lines that name one; file(STRINGS) filters them much faster than a regex over the whole text.
        file(STRINGS "${file}" lines REGEX "(CONFIG|HAVE|ARCH)_")
        # A leading identifier part is matched too, so AV_CODEC_CONFIG_X is one token and dropped below.
        string(REGEX MATCHALL "[A-Za-z0-9_]*(CONFIG|HAVE|ARCH)_[A-Z0-9_]+" found "${lines}")
        list(APPEND names ${found})
        string(REGEX MATCHALL "#[ \t]*define[ \t]+(CONFIG|HAVE|ARCH)_[A-Z0-9_]+" defines "${lines}")
        list(APPEND defined_in_source ${defines})
    endforeach()
    list(FILTER names INCLUDE REGEX "^(CONFIG|HAVE|ARCH)_")
    list(REMOVE_DUPLICATES names)
    list(TRANSFORM defined_in_source REPLACE "^#[ \t]*define[ \t]+" "")
    # HAVE_AV_CONFIG_H is the build's own -D; the others are macros the sources define themselves.
    list(REMOVE_ITEM names HAVE_AV_CONFIG_H ${defined_in_source})
    list(SORT names)
    file(MAKE_DIRECTORY "${gen}")
    file(WRITE "${names_file}" "${names}")
    file(WRITE "${gen}/config-names.key" "${scan_key}")
endif()
file(READ "${names_file}" names)
list(APPEND names ${ffmpeg_on})
list(REMOVE_DUPLICATES names)
list(SORT names)

string(TIMESTAMP this_year "%Y" UTC)
set(config "/* Written by Coney's cmake/ffmpeg/CMakeLists.txt in place of FFmpeg's configure. */\n")
string(APPEND config "#ifndef FFMPEG_CONFIG_H\n#define FFMPEG_CONFIG_H\n")
string(APPEND config "#define FFMPEG_CONFIGURATION \"coney: bink demuxer, bink and binkaudio_dct decoders\"\n")
string(APPEND config "#define FFMPEG_LICENSE \"LGPL version 2.1 or later\"\n")
string(APPEND config "#define CONFIG_THIS_YEAR ${this_year}\n")
string(APPEND config "#define FFMPEG_DATADIR \"\"\n#define AVCONV_DATADIR \"\"\n")
string(APPEND config "#define CC_IDENT \"${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}\"\n")
string(APPEND config "#define OS_NAME ${CMAKE_SYSTEM_NAME}\n#define EXTERN_PREFIX \"\"\n#define EXTERN_ASM\n")
string(APPEND config "#define BUILDSUF \"\"\n#define SLIBSUF \"\"\n#define SWS_MAX_FILTER_SIZE 256\n")
foreach(name IN LISTS names)
    if(name IN_LIST ffmpeg_on)
        string(APPEND config "#define ${name} 1\n")
    else()
        string(APPEND config "#define ${name} 0\n")
    endif()
endforeach()
string(APPEND config "#endif /* FFMPEG_CONFIG_H */\n")
ffmpeg_write("${gen}/config.h" "${config}")
ffmpeg_write("${gen}/config_components.h" "#include \"config.h\"\n")

# The public configuration header and the version string.
ffmpeg_write("${gen}/libavutil/avconfig.h" "#ifndef AVUTIL_AVCONFIG_H\n#define AVUTIL_AVCONFIG_H\n\
#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 0\n#endif /* AVUTIL_AVCONFIG_H */\n")
ffmpeg_write("${gen}/libavutil/ffversion.h" "#ifndef AVUTIL_FFVERSION_H\n#define AVUTIL_FFVERSION_H\n\
#define FFMPEG_VERSION \"${FFMPEG_VERSION}\"\n#endif /* AVUTIL_FFVERSION_H */\n")

# The component lists: exactly the three components, no parser, bitstream filter, muxer or protocol (Coney hands the
# demuxer its own reader, so not even the file protocol). Each is one quoted argument: CMake would split a list at
# the C semicolons.
ffmpeg_write("${gen}/libavcodec/codec_list.c" "static const FFCodec * const codec_list[] = {\n\
    &ff_bink_decoder,\n    &ff_binkaudio_dct_decoder,\n    NULL };\n")
ffmpeg_write("${gen}/libavcodec/parser_list.c" "static const FFCodecParser * const parser_list[] = {\n    NULL };\n")
ffmpeg_write("${gen}/libavcodec/bsf_list.c" "static const FFBitStreamFilter * const bitstream_filters[] = {\n\
    NULL };\n")
ffmpeg_write("${gen}/libavformat/demuxer_list.c" "static const FFInputFormat * const demuxer_list[] = {\n\
    &ff_bink_demuxer,\n    NULL };\n")
ffmpeg_write("${gen}/libavformat/muxer_list.c" "static const FFOutputFormat * const muxer_list[] = {\n    NULL };\n")
ffmpeg_write("${gen}/libavformat/protocol_list.c" "static const URLProtocol * const url_protocols[] = {\n    NULL };\n")

add_library(ffmpeg_bink STATIC ${ffmpeg_sources})
# The generated headers first, so `config.h` and `libavutil/avconfig.h` are found there; then the libraries'
# headers, which include each other as "libavutil/x.h" from the top. Users see a copy of just those headers: the
# source tree's top holds a VERSION file that a case-insensitive file system (macOS) finds for C++'s `<version>`.
set(headers "${CMAKE_CURRENT_BINARY_DIR}/include")
foreach(lib libavutil libavcodec libavformat)
    file(COPY "${src}/${lib}" DESTINATION "${headers}" FILES_MATCHING PATTERN "*.h")
endforeach()
target_include_directories(ffmpeg_bink SYSTEM PUBLIC "${gen}" "${headers}")
target_include_directories(ffmpeg_bink PRIVATE "${src}")
target_compile_definitions(ffmpeg_bink PRIVATE HAVE_AV_CONFIG_H)
set_target_properties(ffmpeg_bink PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(MSVC)
    # As configure sets them up for MSVC: C11 atomics through FFmpeg's Win32 shim (MSVC's own stdatomic.h needs an
    # experimental switch), and its C23 <stdbit.h> fallback. Warnings off: FFmpeg's code is not ours to fix.
    target_include_directories(ffmpeg_bink PRIVATE "${src}/compat/atomics/win32" "${src}/compat/stdbit")
    target_compile_definitions(ffmpeg_bink PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS
                                                   WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600)
    target_compile_options(ffmpeg_bink PRIVATE /W0 /utf-8)
else()
    target_compile_options(ffmpeg_bink PRIVATE -w)
    target_compile_definitions(ffmpeg_bink PRIVATE _ISOC11_SOURCE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(ffmpeg_bink PRIVATE _POSIX_C_SOURCE=200112 _XOPEN_SOURCE=600)
    endif()
    # glibc 2.39 and later have <stdbit.h>; macOS's libc and older glibc do not, so use FFmpeg's fallback everywhere.
    target_include_directories(ffmpeg_bink PRIVATE "${src}/compat/stdbit")
    find_package(Threads REQUIRED)
    target_link_libraries(ffmpeg_bink PUBLIC Threads::Threads)
    if(NOT APPLE)
        target_link_libraries(ffmpeg_bink PUBLIC m)
    endif()
endif()

# FFmpeg relies on the compiler dropping code behind `if (CONFIG_X)` with X off, which only an optimising build is sure
# to do, so FFmpeg is always built optimised, whatever the build type; the debug build of Coney is then also not slowed
# by a debug FFmpeg. These variables are this directory's own, so no other dependency changes.
if(MSVC)
    # /RTC1 (the MSVC Debug default) cannot be combined with /O2.
    foreach(config IN ITEMS DEBUG RELWITHDEBINFO)
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
        string(REGEX REPLACE "/O[bd0-9]+" "" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
        string(APPEND CMAKE_C_FLAGS_${config} " /O2")
    endforeach()
else()
    string(APPEND CMAKE_C_FLAGS_DEBUG " -O2")
endif()
