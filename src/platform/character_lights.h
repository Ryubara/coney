// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_math.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Light;
struct World;
} // namespace rw

namespace coney::platform {

/// The lights Coney's character tools draw with, standing in for the game's LightManager (not yet researched): an
/// ambient light and one directional light, white, in a librw world of their own that holds nothing else. Owns the
/// world, the lights and the directional light's frame. Needs a running RenderEngine and must be destroyed before it
/// stops. Not copyable or movable: librw keeps pointers to it.
class CharacterLights {
  public:
    /// Lights of brightness `ambient` and `directional` (0 to 1), the directional one shining along `direction`
    /// (its direction of travel, in the characters' axes, z up; normalised here).
    CharacterLights(float ambient, float directional, anim::Vec3 direction);
    ~CharacterLights();
    CharacterLights(const CharacterLights&) = delete;
    CharacterLights& operator=(const CharacterLights&) = delete;
    CharacterLights(CharacterLights&&) = delete;
    CharacterLights& operator=(CharacterLights&&) = delete;

    /// Makes these lights the ones librw lights atomics with, until another world is made current.
    void use() const;

  private:
    rw::World* m_world = nullptr;       // owned: librw lights atomics from the current world
    rw::Light* m_ambient = nullptr;     // owned
    rw::Light* m_directional = nullptr; // owned, with its frame
};

} // namespace coney::platform
