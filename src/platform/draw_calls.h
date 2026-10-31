// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "platform/draw_stream.h"

// librw's types, declared rather than included: <rw.h> has no include guard, and the files that call this layer
// include it themselves.
namespace rw {
struct Camera;
struct Image;
struct Matrix;
struct Raster;
struct RGBA;
} // namespace rw

/// Coney's side of the librw boundary: every librw drawing call Coney makes, and each of its direct OpenGL
/// operations as a named one, goes through these functions (docs/engine/renderer.md#comparing). Live, each forwards
/// to librw or OpenGL; under `--headless` (RenderBackend::Null) none does, since there is no OpenGL context, and the
/// layer keeps the render states itself. While a recording runs (startRecording()), each call is also written into a
/// DrawStream, whose hash proves that a change to the renderer left the *PS2* picture alone.
///
/// Routed here: the render states, Im2D, Im3D, the camera's clear, begin, end and show, raster creation, locks and
/// uploads, and the named operations. Not yet: the atomic, clump and human renders, whose pipelines call OpenGL
/// themselves (items 7 to 10 of docs/engine/renderer.md#order). The render states set around them are routed, except
/// in the four functions items 7 to 9 route with their renders (WorldRenderer::render, ParkedCars::draw,
/// PlacedObjects::draw, PlayLighting::drawHumanPasses), which set them on librw directly: a live record's states can be
/// stale after those (docs/engine/renderer.md#comparing).
///
/// What a record holds:
///
/// - a draw (Im2D or Im3D): the primitive type, every render state set so far in the process (a raster by its place
///   in this recording's order of first use, its size and format, and the digest of its pixels where Coney uploaded
///   them while rasters were tracked), the vertices (positions and texture coordinates on the 1/1024 grid, colours at
///   8 bits; Im3D also its normals, the world matrix and the transform flags) and the indices;
/// - a camera step: the clear colour and mode; on begin the camera's matrix, view window, offset, clip planes,
///   projection and frame-buffer size;
/// - a raster's creation (size, depth, format) and each upload (level, size, pixel digest);
/// - a named operation with its rectangle or setting.
///
/// The states are recorded as they stand at each draw, not as calls, so setting a state twice, or setting it again
/// to the value it has, changes no hash; what librw draws with does. librw keeps its render state and Im3D in global
/// state, and so does this layer: one renderer at a time, on one thread.
namespace coney::platform::draw {

/// Forgets everything the layer holds (the render states, rasters, locks, the Im3D vertices, any recording; raster
/// digests off) and sets forwarding (setForwarding()): RenderEngine calls it as it starts, so each renderer, and each
/// test that starts one, begins from the same state.
void reset(bool forwardCalls);

/// Chooses whether calls reach librw and OpenGL (the default) or stop here. RenderEngine sets it as it starts: off for
/// the NULL backend, whose librw device has no rasters and no OpenGL behind it.
void setForwarding(bool on);
/// Whether calls reach librw and OpenGL.
[[nodiscard]] bool forwarding();

/// Chooses whether raster uploads are digested (off by default): the digest of each upload's pixels goes into the
/// records that use the raster. Costs a pass over the pixels of each texture made and each movie frame, so only runs
/// that print a hash turn it on, before their textures load.
void setTrackRasters(bool on);

/// Starts recording into a fresh stream; a recording already running is dropped.
void startRecording();
/// Whether a recording runs.
[[nodiscard]] bool recording();
/// Ends the recording and hands its stream over; nothing when none ran.
std::optional<DrawStream> stopRecording();

/// rw::SetRenderState.
void setRenderState(std::int32_t state, std::uint32_t value);
/// rw::SetRenderStatePtr: the raster for TEXTURERASTER.
void setRenderStatePtr(std::int32_t state, void* value);
/// rw::GetRenderState: librw's own while forwarding, else the layer's.
[[nodiscard]] std::uint32_t getRenderState(std::int32_t state);

/// rw::im2d::GetNearZ: the device's near depth for 2D vertices.
[[nodiscard]] float im2dNearZ();
/// rw::im2d::RenderPrimitive over rw::gl3::Im2DVertex `vertices`.
void im2dRenderPrimitive(int type, void* vertices, std::int32_t numVertices);
/// rw::im2d::RenderIndexedPrimitive over rw::gl3::Im2DVertex `vertices` and 16-bit `indices`.
void im2dRenderIndexedPrimitive(int type, void* vertices, std::int32_t numVertices, void* indices,
                                std::int32_t numIndices);

/// rw::im3d::Transform over rw::gl3::Im3DVertex `vertices`: the vertices the next draws take.
void im3dTransform(void* vertices, std::int32_t numVertices, rw::Matrix* world, std::uint32_t flags);
/// rw::im3d::RenderPrimitive: every transformed vertex in order.
void im3dRenderPrimitive(int type);
/// rw::im3d::RenderIndexedPrimitive with 16-bit `indices` into the transformed vertices.
void im3dRenderIndexedPrimitive(int type, void* indices, std::int32_t numIndices);
/// rw::im3d::End: the transformed vertices released.
void im3dEnd();

/// rw::Camera::clear.
void clearCamera(rw::Camera* camera, rw::RGBA* colour, std::uint32_t mode);
/// rw::Camera::beginUpdate.
void beginCameraUpdate(rw::Camera* camera);
/// rw::Camera::endUpdate.
void endCameraUpdate(rw::Camera* camera);
/// rw::Camera::showRaster.
void showCameraRaster(rw::Camera* camera, std::uint32_t flags);

/// rw::Raster::create for the current platform. Not forwarding, the raster is made with librw's DONTALLOCATE, so it
/// has its size and format and no OpenGL texture.
[[nodiscard]] rw::Raster* createRaster(std::int32_t width, std::int32_t height, std::int32_t depth,
                                       std::int32_t format);
/// rw::Raster::lock of `level` for writing. Not forwarding, a buffer of the level's size kept here until the unlock.
[[nodiscard]] std::uint8_t* lockRaster(rw::Raster* raster, std::int32_t level, std::int32_t lockMode);
/// rw::Raster::unlock: the level's pixels uploaded, and digested while rasters are tracked.
void unlockRaster(rw::Raster* raster, std::int32_t level);
/// rw::Raster::setFromImage at the level `raster` is locked to (0 when it is not): the image's pixels uploaded, and
/// digested as an upload of level 0 whatever the level (a level filled this way and then unlocked is digested twice,
/// the same way every run).
void setRasterFromImage(rw::Raster* raster, rw::Image* image);
/// rw::Raster::convertTexToCurrentPlatform: `source` (destroyed when a new raster is made) as a raster for drawing;
/// null when librw cannot convert it. A PS2 source's texels and palette are its digest.
[[nodiscard]] rw::Raster* convertRasterForDrawing(rw::Raster* source);

/// Copies the window rectangle `x`, `y`, `width`, `height` (from the top left, of a frame `frameHeight` pixels high)
/// into level 0 of `raster` (glCopyTexSubImage2D): the screen passes' copies of the back buffer.
void copyFrameToRaster(rw::Raster* raster, int x, int y, int width, int height, int frameHeight);
/// Reads the frame buffer's rectangle `x`, `y`, `width`, `height` (OpenGL's: from the bottom left) as RGBA rows from
/// the bottom into `rgba`, which holds width * height * 4 bytes (glReadPixels); false when OpenGL reports an error or
/// nothing is forwarded.
[[nodiscard]] bool readFrame(int x, int y, int width, int height, std::uint8_t* rgba);
/// Draws polygons as lines, pulled a little towards the camera, or filled again (glPolygonMode and glPolygonOffset):
/// the mesh views' wireframe.
void setWireframe(bool on);
/// Sends drawing back to the window's own frame buffer (glBindFramebuffer 0).
void bindWindowTarget();
/// Limits the mip levels `raster`'s texture samples to `levels` (GL_TEXTURE_MAX_LEVEL), leaving librw's texture
/// binding as it was.
void setRasterLevels(rw::Raster* raster, std::int32_t levels);

} // namespace coney::platform::draw
