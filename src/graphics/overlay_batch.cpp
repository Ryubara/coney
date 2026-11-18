// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/overlay_batch.h"

namespace coney::graphics {

// Stub until the batching lands: nothing kept, nothing batched.
AlphaRange alphaRangeOf(const Material2d& /*material*/) { return {}; }

OverlayBatch batchOverlays(const FrameDescription& /*frame*/) { return {}; }

Rgba windowClearOf(const FrameStart& start) { return start.clear; }

} // namespace coney::graphics
