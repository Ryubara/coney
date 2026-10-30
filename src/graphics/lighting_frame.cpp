// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/lighting_frame.h"

namespace coney::graphics {

const FrameLight* LightingFrame::light(std::uint16_t /*record*/) const { return nullptr; }

LightingFrame buildLightingFrame(const LevelLighting& /*level*/, const LightView& /*view*/) { return {}; }

std::optional<FrameShadowLight> frameShadowLight(const ShadowLight& /*shadow*/) { return std::nullopt; }

} // namespace coney::graphics
