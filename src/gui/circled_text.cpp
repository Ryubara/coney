// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/circled_text.h"

#include <algorithm>

#include "graphics/overlay_camera.h"

namespace coney::gui {

// Nothing to do beyond the members' own defaults: the parts, the reveal 1 (`+0x50`) and no circles (`+0x5f8`).
CircledText::CircledText() {}

void CircledText::setBack(graphics::SpriteBatch* batch, std::size_t rect) {}

void CircledText::setCircleSprite(graphics::SpriteBatch* batch, std::size_t rect) {}

void CircledText::setup(const CircledTextSetup& setup) {}

void CircledText::setText(std::string_view text) {}

void CircledText::setCircled(std::string_view characters) {}

void CircledText::setCharColour(graphics::Rgba colour) {}

void CircledText::setReveal(float reveal) {}

void CircledText::update(const GuiFrame& frame) {}

void CircledText::render(const GuiCanvas& canvas) const {}

} // namespace coney::gui
