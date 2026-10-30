// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/motion_blur_pass.h"

#include "core/assert.h"
#include "graphics/render_device.h"

namespace coney::platform {

MotionBlurPass::~MotionBlurPass() {
    if (m_engine != nullptr) {
        m_engine->releaseScreenCopy(m_copy);
    }
}

void MotionBlurPass::apply(RenderEngine& engine, effects::MotionBlur::Colour colour) {
    // A copy of its own on the engine it first draws with; a pass draws with one engine all its life.
    // MUTANT (reverted in the next commit): a new copy on every apply(), to show the test catches it.
    m_engine = &engine;
    m_copy = engine.newScreenCopy();
    CONEY_ASSERT(m_engine == &engine);
    engine.addMotionBlur(m_copy, graphics::Rgba{colour.r, colour.g, colour.b, colour.a});
}

} // namespace coney::platform
