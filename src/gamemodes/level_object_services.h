// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "scripting/script_system.h"
#include "world_objects/flags.h"
#include "world_objects/object_services.h"

namespace coney {

/// What gameplay gives the level's glass panes, doors and barriers of the world they change beyond its triangles and
/// links: the lock pick's script callbacks, the `CrimeScene` flag a break-in moves, and the sounds, which go to
/// `sounds` (the audio's ObjectSounds; null: none). The rest of ObjectServices (shards, crimes, statistics, loose
/// objects, models) does nothing until those systems are in Coney.
///
/// Research: docs/research/objects.md#coneys-implementation, docs/research/crimes.md#lockpick
class LevelObjectServices final : public world_objects::ObjectServices {
  public:
    /// Over `scripts` and `flags`, which must outlive it; sounds to `sounds` (may be null).
    LevelObjectServices(script::ScriptSystem& scripts, world_objects::WorldFlags& flags,
                        world_objects::ObjectServices* sounds)
        : m_scripts(scripts), m_flags(flags), m_sounds(sounds) {}

    /// Sounds go to `sounds` from now on (null: none).
    void setSounds(world_objects::ObjectServices* sounds) { m_sounds = sounds; }

    void playSound(std::uint32_t nameHash, anim::Vec3 at) override;
    void playMaterialPair(std::uint8_t a, std::uint8_t b, anim::Vec3 at) override;
    void lockPickClick(double human) override;
    /// Calls the script function `function` with the human's and the door's handles; nothing for an empty name.
    void callScript(std::string_view function, double human, double door) override;
    /// Moves the level's `CrimeScene` flag (kCrimeSceneFlag) to `at`.
    void moveCrimeSceneFlag(anim::Vec3 at) override;

  private:
    script::ScriptSystem& m_scripts;
    world_objects::WorldFlags& m_flags;
    world_objects::ObjectServices* m_sounds;
};

} // namespace coney
