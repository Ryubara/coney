// SPDX-License-Identifier: GPL-3.0-or-later
// Stub: the draw layer is not written yet (its tests come first). Nothing is forwarded or recorded.
#include "platform/draw_calls.h"

namespace coney::platform::draw {

void reset(bool /*forwardCalls*/) {}
void setForwarding(bool /*on*/) {}
bool forwarding() { return false; }
void setTrackRasters(bool /*on*/) {}
void startRecording() {}
bool recording() { return false; }
std::optional<DrawStream> stopRecording() { return std::nullopt; }
void setRenderState(std::int32_t /*state*/, std::uint32_t /*value*/) {}
void setRenderStatePtr(std::int32_t /*state*/, void* /*value*/) {}
std::uint32_t getRenderState(std::int32_t /*state*/) { return 0; }
float im2dNearZ() { return 0.0F; }
void im2dRenderPrimitive(int /*type*/, void* /*vertices*/, std::int32_t /*numVertices*/) {}
void im2dRenderIndexedPrimitive(int /*type*/, void* /*vertices*/, std::int32_t /*numVertices*/, void* /*indices*/,
                                std::int32_t /*numIndices*/) {}
void im3dTransform(void* /*vertices*/, std::int32_t /*numVertices*/, rw::Matrix* /*world*/, std::uint32_t /*flags*/) {}
void im3dRenderPrimitive(int /*type*/) {}
void im3dRenderIndexedPrimitive(int /*type*/, void* /*indices*/, std::int32_t /*numIndices*/) {}
void im3dEnd() {}
void clearCamera(rw::Camera* /*camera*/, rw::RGBA* /*colour*/, std::uint32_t /*mode*/) {}
void beginCameraUpdate(rw::Camera* /*camera*/) {}
void endCameraUpdate(rw::Camera* /*camera*/) {}
void showCameraRaster(rw::Camera* /*camera*/, std::uint32_t /*flags*/) {}
rw::Raster* createRaster(std::int32_t /*width*/, std::int32_t /*height*/, std::int32_t /*depth*/,
                         std::int32_t /*format*/) {
    return nullptr;
}
std::uint8_t* lockRaster(rw::Raster* /*raster*/, std::int32_t /*level*/, std::int32_t /*lockMode*/) { return nullptr; }
void unlockRaster(rw::Raster* /*raster*/, std::int32_t /*level*/) {}
void setRasterFromImage(rw::Raster* /*raster*/, rw::Image* /*image*/) {}
rw::Raster* convertRasterForDrawing(rw::Raster* /*source*/) { return nullptr; }
void copyFrameToRaster(rw::Raster* /*raster*/, int /*x*/, int /*y*/, int /*width*/, int /*height*/,
                       int /*frameHeight*/) {}
bool readFrame(int /*x*/, int /*y*/, int /*width*/, int /*height*/, std::uint8_t* /*rgba*/) { return false; }
void setWireframe(bool /*on*/) {}
void bindWindowTarget() {}
void setRasterLevels(rw::Raster* /*raster*/, std::int32_t /*levels*/) {}

} // namespace coney::platform::draw
