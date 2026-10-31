// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace coney::platform {

/// What one record of a draw stream is: a draw, a camera step, a raster's life or one of Coney's named OpenGL
/// operations. The value is written into the stream, so a renumbering changes every hash.
enum class DrawRecord : std::uint8_t {
    Im2DDraw = 1,      ///< rw::im2d::RenderPrimitive or RenderIndexedPrimitive, with the states in force
    Im3DDraw = 2,      ///< rw::im3d::RenderPrimitive or RenderIndexedPrimitive of the last Transform, with the states
    CameraClear = 3,   ///< rw::Camera::clear: the colour and what is cleared
    CameraBegin = 4,   ///< rw::Camera::beginUpdate: the camera's place, view window and clip planes
    CameraEnd = 5,     ///< rw::Camera::endUpdate
    CameraShow = 6,    ///< rw::Camera::showRaster: the frame shown
    RasterCreate = 7,  ///< rw::Raster::create: the raster's size and format
    RasterUpload = 8,  ///< a raster's level written through lock and unlock: the size and a digest of its pixels
    FrameCopy = 9,     ///< a window rectangle copied into a raster (glCopyTexSubImage2D)
    FrameRead = 10,    ///< a window rectangle read back (glReadPixels)
    Wireframe = 11,    ///< polygons drawn as lines or filled again (glPolygonMode)
    WindowTarget = 12, ///< drawing sent back to the window's frame buffer (glBindFramebuffer 0)
    RasterLevels = 13, ///< the mip levels a raster samples (GL_TEXTURE_MAX_LEVEL)
};

/// How many records of each kind a stream holds, and how many vertices its draws had, for the line a run prints.
struct DrawCounts {
    std::uint32_t records = 0; ///< every record, of any kind
    std::uint32_t im2dDraws = 0;
    std::uint32_t im3dDraws = 0;
    std::uint32_t vertices = 0;  ///< over every Im2D and Im3D draw
    std::uint32_t cameraOps = 0; ///< clears, begins, ends and shows
    std::uint32_t rasterOps = 0; ///< creations, uploads and mip levels
    std::uint32_t frameOps = 0;  ///< copies, read-backs, wireframe switches and window targets
};

/// The grid a draw stream rounds to: positions and texture coordinates to 1/1024 of a unit, colours to 8 bits a channel
/// (docs/engine/renderer.md#comparing). Values a little apart that round to the same step hash the same, so a change
/// in the last bits of a float (another compiler, another order of the same sums) does not move the hash.
inline constexpr double kDrawGridSteps = 1024.0;

/// A position or texture coordinate on the draw stream's grid: the nearest whole number of 1/1024 steps. NaN is one
/// value of its own, and infinities and values past 2^40 steps are clamped there, so no input is undefined.
[[nodiscard]] std::int64_t drawGridValue(float value);

/// A colour channel from 0 to 1 at 8 bits: the nearest of 0 to 255, clamped (NaN is 0).
[[nodiscard]] std::uint8_t drawColourChannel(float value);

/// The recording of what Coney asked librw and OpenGL to draw (platform/draw_calls.h), reduced to a hash that is the
/// same on every system for the same drawing: each record is written as bytes in a fixed order and width (little
/// endian, never a struct's memory), floats on the grid above, and the bytes are hashed with 64-bit FNV-1a as they
/// come, so a stream of any length costs only its counts and one hash per record.
///
/// A record starts with begin() and holds whatever its kind writes after it; the per-record hashes (recordHashes())
/// say which record two streams first differ at. The original has nothing like it: it is Coney's check that its own
/// renderer work leaves the *PS2* picture alone.
class DrawStream {
  public:
    /// Starts a record of `kind`.
    void begin(DrawRecord kind);
    /// An exact integer: a state's value, a count, an index, a size.
    void word(std::uint32_t value);
    /// A position or texture coordinate, rounded to the grid (drawGridValue()).
    void gridValue(float value);
    /// A colour, 8 bits a channel.
    void colour(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);
    /// A digest computed elsewhere (a raster's pixels: pixelDigest()).
    void digest(std::uint64_t value);
    /// Adds `count` vertices to the counts (a draw's).
    void countVertices(std::uint32_t count) { m_counts.vertices += count; }

    /// The hash of every byte written so far; the FNV-1a offset basis for an empty stream.
    [[nodiscard]] std::uint64_t hash() const { return m_hash; }
    /// The records' kinds counted.
    [[nodiscard]] const DrawCounts& counts() const { return m_counts; }
    /// One hash per record, of that record's bytes alone, in order.
    [[nodiscard]] std::vector<std::uint64_t> recordHashes() const;

  private:
    // Feeds `bytes` to the stream's hash and the current record's.
    void feed(std::span<const std::uint8_t> bytes);
    // Feeds `value` as `width` little-endian bytes.
    void feedLittleEndian(std::uint64_t value, int width);

    std::uint64_t m_hash = 0xcbf29ce484222325ULL; // FNV-1a 64's offset basis
    std::vector<std::uint64_t> m_records;         // each record's own running hash
    DrawCounts m_counts;
};

/// The 64-bit FNV-1a hash of `bytes`, for a raster's pixels in a RasterUpload record.
[[nodiscard]] std::uint64_t pixelDigest(std::span<const std::uint8_t> bytes);

} // namespace coney::platform
