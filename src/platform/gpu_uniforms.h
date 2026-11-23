// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace coney::platform {

/// The 2D vertex shader's uniform: the target's size in pixels. Padded to 16 bytes, as SDL's uniform blocks are laid.
struct FrameUniform {
    float width = 0.0F;
    float height = 0.0F;
    float unused[2] = {0.0F, 0.0F};
};

/// The 2D fragment shader's uniform: the alpha range a draw keeps (graphics::AlphaRange). Padded as FrameUniform.
struct AlphaUniform {
    float low = 0.0F;
    float high = 0.0F;
    float unused[2] = {0.0F, 0.0F};
};

} // namespace coney::platform
