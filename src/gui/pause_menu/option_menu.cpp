// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs that let the Options screen's tests compile before the screen is written: each returns a default and does
// nothing (docs/guides/stamps.md#fails-first).
#include "gui/pause_menu/option_menu.h"

#include <string>
#include <vector>

namespace coney::gui {

// Stub: the default values, nothing restored yet.
OptionValues OptionValues::defaults(bool /*wideVideo*/) { return {}; }

// Stub: the default layout for every mode.
OptionLayout OptionLayout::forMode(Language /*language*/, bool /*widescreen*/, bool /*flag02*/) { return {}; }

// Stub: nothing set up.
OptionMenu::OptionMenu() = default;

// Stub: opens nothing.
void OptionMenu::open(const GlobalStrings* /*strings*/, MenuInput& /*input*/, Hooks /*hooks*/,
                      const OptionValues& /*values*/, const OptionLayout& /*layout*/, int /*level*/,
                      std::uint64_t /*nowMs*/) {}

// Stub: nothing to close.
void OptionMenu::close() {}

// Stub: takes no command.
bool OptionMenu::handle(MenuCommand /*command*/) { return false; }

// Stub: nothing to advance.
void OptionMenu::update(const GuiFrame& /*frame*/) {}

// Stub: draws nothing.
void OptionMenu::render(const GuiCanvas& /*canvas*/) const {}

// Stub: keeps no fade.
void OptionMenu::setFade(float /*fade*/) {}

// Stub: keeps no batch.
void OptionMenu::setSpriteBatch(graphics::SpriteBatch* /*batch*/) {}

// Stub: shows no text.
std::vector<std::string> OptionMenu::lines() const { return {}; }

} // namespace coney::gui
