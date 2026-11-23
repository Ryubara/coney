// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/screen_pass_steps.h"

namespace coney::graphics {

ScreenSteps screenStepsOf(const ScreenPass& /*pass*/, bool /*keptReady*/) { return {}; }

std::vector<ScreenStep> blurPulseSteps(const BlurPulse& /*blur*/, const ScreenRect& /*view*/) { return {}; }

} // namespace coney::graphics
