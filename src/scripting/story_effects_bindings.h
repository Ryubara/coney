// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

// The camera, particle and sound bindings the story's second and third missions add: the cameras' clipping, the active
// camera and its place, the follow camera put at a point, the scripted path camera; particle systems switched on and
// off; the system music's switch and tracks; and the reverb's settings. Each acts on the level's cameras, particles and
// sound through the binding context as it is at the call. Research: docs/references/bindings/camera.md,
// docs/references/bindings/effects.md, docs/references/bindings/sound.md

namespace coney::script {

/// The bindings registered here; installBindings() registers them with addStoryEffectsBindings().
inline constexpr std::array<std::string_view, 18> kStoryEffectsBindings{"CamAddPoizoPoint",
                                                                        "CamAddPoizoPointCam",
                                                                        "CameraGetActive",
                                                                        "CameraSetClipping",
                                                                        "CamGetPos",
                                                                        "CamSetFollowPos",
                                                                        "CamSetupPoizo",
                                                                        "CfgSteam",
                                                                        "EndGarbage",
                                                                        "EndParticle",
                                                                        "MaxFogParticles",
                                                                        "SoundEnableEffects",
                                                                        "SoundEnableSystemMusic",
                                                                        "SoundSetEffect",
                                                                        "SoundSetMusicTrack",
                                                                        "Start3DFog",
                                                                        "StartGarbage",
                                                                        "StartParticle"};

/// Registers kStoryEffectsBindings in `vm`: the cameras on `context.cameras`, the particles, steam vents, fog and
/// litter on `context.effects` (and a plain object's record on `context.spawnRecords`), the music and reverb on
/// `context.state` and `context.sound`. Each does nothing (or answers nil) without what it acts on. `nextHandle` gives
/// the path camera its handle.
///
/// Research: docs/references/bindings/story.md
void addStoryEffectsBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle);

} // namespace coney::script
