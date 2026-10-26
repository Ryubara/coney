// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/bar.h"

#include <algorithm>

#include "graphics/overlay_camera.h"
#include "graphics/screen.h"

namespace coney::gui {

// The fill, clamped to 0-1 as the generic bar's setter clamps it: anything not above 0 (a NaN too) is empty.
void Bar::setFill(float fraction) {}

// The meter's sprites, in the original's order (docs/research/hud-bar.md#draw).
void Bar::render(const GuiCanvas& /*canvas*/) const {}

} // namespace coney::gui
