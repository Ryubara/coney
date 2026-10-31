// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/draw_stream.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace coney::platform {

namespace {

constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;
// The largest grid value kept: 2^40 steps, far past any coordinate Coney draws, well inside an int64.
constexpr double kGridLimit = 1099511627776.0;
// The grid value NaN gets: below every clamped value, so it is never confused with one.
constexpr std::int64_t kGridNan = -(1LL << 41);

// One FNV-1a step over `bytes`, from `hash`.
std::uint64_t fnv(std::uint64_t hash, std::span<const std::uint8_t> bytes) {
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

std::int64_t drawGridValue(float value) {
    if (std::isnan(value)) {
        return kGridNan;
    }
    // In double, so the scaling itself rounds nothing: a float times 1024 is exact.
    const double scaled = std::clamp(static_cast<double>(value) * kDrawGridSteps, -kGridLimit, kGridLimit);
    return std::llround(scaled);
}

std::uint8_t drawColourChannel(float value) {
    if (std::isnan(value)) {
        return 0;
    }
    return static_cast<std::uint8_t>(std::lround(std::clamp(static_cast<double>(value), 0.0, 1.0) * 255.0));
}

void DrawStream::begin(DrawRecord kind) {
    m_records.push_back(kFnvOffset);
    ++m_counts.records;
    // The counts the run prints, by kind.
    switch (kind) {
    case DrawRecord::Im2DDraw:
        ++m_counts.im2dDraws;
        break;
    case DrawRecord::Im3DDraw:
        ++m_counts.im3dDraws;
        break;
    case DrawRecord::CameraClear:
    case DrawRecord::CameraBegin:
    case DrawRecord::CameraEnd:
    case DrawRecord::CameraShow:
        ++m_counts.cameraOps;
        break;
    case DrawRecord::RasterCreate:
    case DrawRecord::RasterUpload:
    case DrawRecord::RasterLevels:
        ++m_counts.rasterOps;
        break;
    case DrawRecord::FrameCopy:
    case DrawRecord::FrameRead:
    case DrawRecord::Wireframe:
    case DrawRecord::WindowTarget:
        ++m_counts.frameOps;
        break;
    }
    feedLittleEndian(static_cast<std::uint8_t>(kind), 1);
}

void DrawStream::word(std::uint32_t value) { feedLittleEndian(value, 4); }

void DrawStream::gridValue(float value) { feedLittleEndian(static_cast<std::uint64_t>(drawGridValue(value)), 8); }

void DrawStream::colour(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    const std::array<std::uint8_t, 4> bytes{r, g, b, a};
    feed(bytes);
}

void DrawStream::digest(std::uint64_t value) { feedLittleEndian(value, 8); }

std::vector<std::uint64_t> DrawStream::recordHashes() const { return m_records; }

void DrawStream::feed(std::span<const std::uint8_t> bytes) {
    m_hash = fnv(m_hash, bytes);
    if (!m_records.empty()) {
        m_records.back() = fnv(m_records.back(), bytes);
    }
}

void DrawStream::feedLittleEndian(std::uint64_t value, int width) {
    std::array<std::uint8_t, 8> bytes{};
    for (int i = 0; i < width; ++i) {
        bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    feed(std::span<const std::uint8_t>(bytes.data(), static_cast<std::size_t>(width)));
}

std::uint64_t pixelDigest(std::span<const std::uint8_t> bytes) { return fnv(kFnvOffset, bytes); }

} // namespace coney::platform
