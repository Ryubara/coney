// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/effect_batch.h"

namespace coney::graphics {

EffectLook effectLookOf(const EffectDraw& /*draw*/, bool /*textureAlpha*/) { return {}; }

MeshVertex meshVertexOf(const EffectVertex& /*vertex*/) { return {}; }

} // namespace coney::graphics
