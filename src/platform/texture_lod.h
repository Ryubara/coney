// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace rw {
struct Raster;
}

namespace coney::platform {

/// Coney's texture level selection for librw's GL3 atomics, as the GS does it (graphics::TextureLod): OpenGL picks a
/// mip level by the texture's size on screen, the GS by distance. Atomics drawn through librw's default GL3 pipeline
/// go through Coney's own shaders, which sample at an explicit level: by the distance and the texture's `K` while
/// distance mipmaps are on (the streamed world's sectors), else level 0 (everything else, which the original draws
/// bilinear). Skinned humans keep librw's skin pipeline; their textures have no mipmaps. Textures not converted from
/// a PS2 raster (Coney's own, such as the sandbox's) keep OpenGL's choice.
///
/// Research: docs/research/rendering.md#world

/// Registers the raster plugin that keeps each converted texture's `K` and `L`, and the shaders' uniform. Between
/// librw's Engine::init and Engine::open, with the other plugins.
void attachTextureLodPlugin();

/// Gives a converted raster the `K` and `L` its PS2 raster carried (the packed word librw keeps for it).
void setRasterLod(rw::Raster* raster, std::uint32_t packedKl);

/// With an OpenGL 3.3 (or ES 3.1) context: makes the shaders and puts Coney's render step into librw's default GL3
/// pipeline. False when the context's shading language cannot sample at a level (OpenGL 2.1, ES 2), in which case
/// librw's own step stays and OpenGL picks levels. After Engine::start.
bool startTextureLod();

/// Puts librw's render step back and destroys the shaders. Before Engine::stop; harmless when not started.
void stopTextureLod();

/// Chooses how the next atomics pick their texture levels: by distance (the streamed world) or level 0 (the rest).
void setDistanceMipmaps(bool on);

} // namespace coney::platform
