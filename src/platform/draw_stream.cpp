// SPDX-License-Identifier: GPL-3.0-or-later
// Stub: the draw stream is not written yet (its tests come first).
#include "platform/draw_stream.h"

namespace coney::platform {

std::int64_t drawGridValue(float /*value*/) { return 0; }

std::uint8_t drawColourChannel(float /*value*/) { return 0; }

void DrawStream::begin(DrawRecord /*kind*/) {}

void DrawStream::word(std::uint32_t /*value*/) {}

void DrawStream::gridValue(float /*value*/) {}

void DrawStream::colour(std::uint8_t /*r*/, std::uint8_t /*g*/, std::uint8_t /*b*/, std::uint8_t /*a*/) {}

void DrawStream::digest(std::uint64_t /*value*/) {}

std::vector<std::uint64_t> DrawStream::recordHashes() const { return {}; }

void DrawStream::feed(std::span<const std::uint8_t> /*bytes*/) {}

void DrawStream::feedLittleEndian(std::uint64_t /*value*/, int /*width*/) {}

std::uint64_t pixelDigest(std::span<const std::uint8_t> /*bytes*/) { return 0; }

} // namespace coney::platform
