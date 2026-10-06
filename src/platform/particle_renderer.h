// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string_view>

#include "core/chunk_system.h"
#include "effects/particles.h"
#include "fileio/wad.h"
#include "graphics/particle_page.h"
#include "world/view_frustum.h"

namespace coney::platform {

/// Draws a level's particle systems (effects::ParticleSystems) as sprites facing the camera, each a rectangle of its
/// type's sprite sheet (docs/research/particles.md#sprite-words) tinted by its colour, with librw's 3D immediate mode.
/// The sheets are loaded from the disc the first time a sprite needs one; one that does not load is reported once
/// through `print`, and its sprites are drawn as flat-coloured quads.
///
/// **Coney's stand-ins** for the original's sprite batches (`PTank`), whose drawing is not traced: every sprite a
/// square facing the camera; the glows, flashes, flames and sparks added to what is behind them, the rest blended by
/// alpha; tested against depth but not writing it, after the scenery and the characters.
class ParticleRenderer {
  public:
    /// Loads sheets from `wad`, their textures converted for drawing when `forDrawing` (an engine that draws pixels).
    /// Needs a running RenderEngine; must be destroyed before it stops.
    ParticleRenderer(const io::Wad& wad, bool forDrawing, std::function<void(std::string_view)> print);
    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;
    ParticleRenderer(ParticleRenderer&&) = delete;
    ParticleRenderer& operator=(ParticleRenderer&&) = delete;
    ~ParticleRenderer();

    /// Draws every shown system of `systems` through the current camera, which is at `view` (RenderWare's axes).
    void draw(const effects::ParticleSystems& systems, const world::CameraPose& view);

  private:
    // The sheet `sheet` names, loading it on first use; null when it is ParticleSheet::None or did not load.
    const graphics::SpriteSheet* sheet(effects::ParticleSheet sheet);

    const io::Wad& m_wad;
    bool m_forDrawing;
    std::function<void(std::string_view)> m_print;
    chunk::ChunkHandlerTable m_table;
    std::map<effects::ParticleSheet, std::unique_ptr<graphics::SpriteSheet>> m_sheets; // null: tried and failed
};

} // namespace coney::platform
