// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/story_effects_bindings.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "camera/cameras.h"
#include "camera/path_camera.h"
#include "core/name_hash.h"
#include "effects/ground_fog.h"
#include "effects/level_effects.h"
#include "effects/particles.h"
#include "scripting/binding_args.h"
#include "scripting/lua_value.h"
#include "scripting/sound_bindings.h"
#include "warriors/game_state.h"
#include "world_objects/radios.h"
#include "world_objects/spawn_records.h"

namespace coney::script {

namespace {

// `NilHandle`'s value.
constexpr double kNilHandle = 0.0;

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as a handle: truncated to an unsigned 32-bit integer.
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(static_cast<std::uint32_t>(wholeArg(args, i)));
}

// Whether argument `i` is missing or nil, so its default applies.
bool absent(std::span<const Value> args, std::size_t i) { return i >= args.size() || args[i].isNil(); }

// Argument `i` as a boolean as tolua reads one: nil and false are false, a number is its value's truth.
bool boolArg(std::span<const Value> args, std::size_t i) { return !absent(args, i) && binding::number(args, i) != 0.0; }

// An M_Vector4 (x, y, z and w 1) as a table.
binding::Results vectorResult(anim::Vec3 p) {
    auto vector = std::make_shared<Table>();
    for (const auto& [field, value] :
         {std::pair{"x", p.x}, std::pair{"y", p.y}, std::pair{"z", p.z}, std::pair{"w", 1.0F}}) {
        if (auto set = vector->set(Value(std::string(field)), Value(static_cast<double>(value))); !set) {
            return std::unexpected(set.error());
        }
    }
    return std::vector<Value>{Value(std::move(vector))};
}

// `CameraSetClipping(camera, near, far)`.
// @orig 0x0011bb98 Camera_SetClipping (unknown)
NativeFunction makeCameraSetClipping(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->cameras != nullptr) {
            context->cameras->setClipping(handleArg(args, 0), static_cast<float>(binding::number(args, 1)),
                                          static_cast<float>(binding::number(args, 2)));
        }
        return binding::none();
    };
}

// `CameraGetActive(player) -> camera`: player 1's current camera, NilHandle for none. **Coney choice**: Coney has one
// player, so any other index answers NilHandle.
// @orig 0x0011b838 Camera_GetActiveHandle (unknown)
NativeFunction makeCameraGetActive(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const bool first = absent(args, 0) || wholeArg(args, 0) == 0;
        const std::optional<double> active =
            context->cameras != nullptr && first ? context->cameras->activeHandle() : std::nullopt;
        return binding::number(active.value_or(kNilHandle));
    };
}

// `CamGetPos(camera) -> M_Vector4`: where the camera is; the zero vector for a handle that names no camera.
// @orig 0x0011b920 Camera_GetPositionByHandle (unknown)
NativeFunction makeCamGetPos(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const std::optional<anim::Vec3> at =
            context->cameras != nullptr ? context->cameras->positionOf(handleArg(args, 0)) : std::nullopt;
        return vectorResult(at.value_or(anim::Vec3{}));
    };
}

// `CamSetFollowPos(pos, player)`: player 1's follow camera put there at once; the table is left as it is.
// @orig 0x0011c638 Camera_SetFollowPosition (unknown)
NativeFunction makeCamSetFollowPos(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const std::optional<std::array<float, 3>> at = binding::position(args, 0);
        const bool first = absent(args, 1) || wholeArg(args, 1) == 0;
        if (context->cameras != nullptr && at && first) {
            context->cameras->setFollowPosition(anim::Vec3{(*at)[0], (*at)[1], (*at)[2]});
        }
        return binding::none();
    };
}

// Argument `i` as a function name: empty for nil or a missing argument.
std::string nameArg(std::span<const Value> args, std::size_t i) {
    return absent(args, i) ? std::string() : binding::string(args, i);
}

// `CamSetupPoizo(camera, seconds, onEnd, fov, far, human) -> camera`: the path camera started from `camera`'s view;
// NilHandle when `camera` names none. **Coney choice**: Coney has one player, so `human` (another player's camera of
// the same kind) is not read.
// @orig 0x0011c9e0 Camera_SetupPoizo (unknown)
NativeFunction makeCamSetupPoizo(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        if (context->cameras == nullptr) {
            return binding::number(kNilHandle);
        }
        const std::optional<double> path = context->cameras->setupPath(
            context->cameras->path() == nullptr ? nextHandle() : 0.0, handleArg(args, 0),
            static_cast<float>(binding::number(args, 1)), nameArg(args, 2),
            static_cast<float>(binding::number(args, 3)), static_cast<float>(binding::number(args, 4)));
        return binding::number(path.value_or(kNilHandle));
    };
}

// `CamAddPoizoPoint(pos, heading, pitch, roll, seconds, onReach) -> boolean`; the table is left as it is.
// @orig 0x0011cbb0 Camera_AddPoizoPoint (unknown)
NativeFunction makeCamAddPoizoPoint(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const std::array<float, 3> at = binding::position(args, 0).value_or(std::array<float, 3>{});
        const camera::PathPoint point{.position = anim::Vec3{at[0], at[1], at[2]},
                                      .orientation =
                                          camera::orientationOf(static_cast<float>(binding::number(args, 1)),
                                                                static_cast<float>(binding::number(args, 2)),
                                                                static_cast<float>(binding::number(args, 3))),
                                      .seconds = static_cast<float>(binding::number(args, 4)),
                                      .onReach = nameArg(args, 5)};
        return binding::boolean(context->cameras != nullptr && context->cameras->addPathPoint(point));
    };
}

// `CamAddPoizoPointCam(camera, seconds, onReach) -> boolean`.
// @orig 0x0011cc68 Camera_AddPoizoPointCam (unknown)
NativeFunction makeCamAddPoizoPointCam(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        return binding::boolean(context->cameras != nullptr &&
                                context->cameras->addPathPointFrom(handleArg(args, 0),
                                                                   static_cast<float>(binding::number(args, 1)),
                                                                   nameArg(args, 2)));
    };
}

// `StartParticle(object)` / `EndParticle(object)`: message `0x12` or `0x13` to the object: a particle system starts or
// stops making sprites, a plain object (a spawn record) is shown or hidden.
// @orig 0x003975c0 Particle_Start (unknown)
// @orig 0x00397610 Particle_End (unknown)
NativeFunction makeParticleSwitch(const BindingContext& context, bool on) {
    return [context = &context, on](std::span<const Value> args) {
        const double handle = handleArg(args, 0);
        if (context->effects != nullptr && context->effects->particles.setEmitting(handle, on)) {
            return binding::none();
        }
        if (context->spawnRecords != nullptr) {
            if (world_objects::SpawnRecord* record = context->spawnRecords->resolve(handle); record != nullptr) {
                record->hidden = !on;
            }
        }
        return binding::none();
    };
}

// `SoundEnableSystemMusic(on)`: a change forces a new pick (the next frame's step); off stops the music.
// @orig 0x00113ea8 Sound_EnableSystemMusic (unknown)
NativeFunction makeSoundEnableSystemMusic(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        StoryState& story = context->state->story;
        const bool on = boolArg(args, 0);
        if (on == story.systemMusic) {
            return binding::none();
        }
        story.systemMusic = on;
        story.musicMood = -1;
        if (!on && context->sound != nullptr) {
            context->sound->stopMusic();
        }
        return binding::none();
    };
}

// `SoundSetMusicTrack(slot, track1, track2, track3)`: the mood's tracks (the non-empty names' hashes, in order), and
// the system music on; the mood playing is picked again. **Coney choice**: a slot outside 0-2 does nothing (the
// original does not check it).
// @orig 0x00113ed0 Sound_SetMusicTrack (unknown)
NativeFunction makeSoundSetMusicTrack(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        StoryState& story = context->state->story;
        const std::int64_t slot = wholeArg(args, 0);
        if (slot < 0 || slot >= static_cast<std::int64_t>(kMusicMoods)) {
            return binding::none();
        }
        std::vector<std::uint32_t>& tracks = story.moodTracks.at(static_cast<std::size_t>(slot));
        tracks.clear();
        for (std::size_t i = 1; i <= kMoodTracks; ++i) {
            if (const std::string name = absent(args, i) ? std::string() : binding::string(args, i); !name.empty()) {
                tracks.push_back(crc32(name));
            }
        }
        story.systemMusic = true;
        if (story.musicMood == slot) {
            story.musicMood = -1;
        }
        return binding::none();
    };
}

// `SoundSetEffect(effect, depth, delay, feedback)`: the reverb's settings, kept (**Coney stand-in**: no reverb yet).
// @orig 0x00114028 Sound_SetEffect (unknown)
NativeFunction makeSoundSetEffect(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        StoryState& story = context->state->story;
        story.reverbType = static_cast<int>(wholeArg(args, 0));
        story.reverbDepth = static_cast<float>(binding::number(args, 1));
        story.reverbDelay = static_cast<int>(wholeArg(args, 2));
        story.reverbFeedback = static_cast<int>(wholeArg(args, 3));
        return binding::none();
    };
}

// `SoundEnableEffects(on)`: the reverb on (the default, when the argument is left out) or off, kept (**Coney
// stand-in**).
// @orig 0x00114060 Sound_EnableEffects (unknown)
NativeFunction makeSoundEnableEffects(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        context->state->story.reverbOn = args.empty() || boolArg(args, 0);
        return binding::none();
    };
}

// Argument `i` as a colour table `{r, g, b, a}` (t[1]..t[4], each truncated to a byte); zeros for a missing table or
// entry.
std::array<std::uint8_t, 4> colourArg(std::span<const Value> args, std::size_t i) {
    std::array<std::uint8_t, 4> colour{};
    if (i >= args.size() || args[i].table() == nullptr) {
        return colour;
    }
    const Table& table = *args[i].table();
    for (std::size_t c = 0; c < colour.size(); ++c) {
        const double value = table.get(Value(static_cast<double>(c + 1))).number().value_or(0.0);
        colour.at(c) = static_cast<std::uint8_t>(static_cast<std::int64_t>(std::trunc(value)));
    }
    return colour;
}

// `Start3DFog(texture, colour, drift, fadeSpeed, fadeRate)`: the ground fog started over (the colour table is left as
// it is).
// @orig 0x0018e148 Fog3D_Start (unknown)
NativeFunction makeStart3DFog(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->effects != nullptr) {
            context->effects->fog.start(effects::FogSettings{.sprite = static_cast<std::uint32_t>(wholeArg(args, 0)),
                                                             .colour = colourArg(args, 1),
                                                             .drift = static_cast<float>(binding::number(args, 2)),
                                                             .fadeSpeed = static_cast<float>(binding::number(args, 3)),
                                                             .fadeRate = static_cast<float>(binding::number(args, 4))});
        }
        return binding::none();
    };
}

// `MaxFogParticles(count)`: message `0x22` to the fog's emitter; nothing with no fog running. **Coney's reading**: the
// emitter's handling (replacing the 20-wisp top-up) is inferred on the page.
// @orig 0x0018e2f0 Fog3D_SetMaxParticles (unknown)
NativeFunction makeMaxFogParticles(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->effects != nullptr) {
            context->effects->fog.setMaxWisps(static_cast<std::uint32_t>(wholeArg(args, 0)));
        }
        return binding::none();
    };
}

// `StartGarbage(kind)` / `EndGarbage()`: the blowing litter round the camera.
// @orig 0x003977a8 Garbage_Start (unknown)
// @orig 0x003977d0 Garbage_End (unknown)
NativeFunction makeGarbage(const BindingContext& context, bool start) {
    return [context = &context, start](std::span<const Value> args) {
        if (context->effects != nullptr) {
            if (start) {
                context->effects->litter.start(static_cast<std::uint32_t>(wholeArg(args, 0)));
            } else {
                context->effects->litter.end();
            }
        }
        return binding::none();
    };
}

// `CfgSteam(object, colour, interval, puffInterval, size, growth, life, speed, rise, dragH, dragV, still)`: message
// `0x27` to a steam vent (the colour table is left as it is).
// @orig 0x0039be28 Steam_Configure (unknown)
NativeFunction makeCfgSteam(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->effects == nullptr) {
            return binding::none();
        }
        const std::array<std::uint8_t, 4> c = colourArg(args, 1);
        const auto f = [&args](std::size_t i) { return static_cast<float>(binding::number(args, i)); };
        static_cast<void>(context->effects->particles.configureSteam(
            handleArg(args, 0),
            effects::SteamSettings{.colour = (std::uint32_t{c[0]} << 24U) | (std::uint32_t{c[1]} << 16U) |
                                             (std::uint32_t{c[2]} << 8U) | std::uint32_t{c[3]},
                                   .interval = static_cast<std::uint32_t>(wholeArg(args, 2)),
                                   .puffInterval = static_cast<std::uint32_t>(wholeArg(args, 3)),
                                   .size = f(4),
                                   .growth = f(5),
                                   .life = f(6),
                                   .speed = f(7),
                                   .rise = f(8),
                                   .dragH = f(9),
                                   .dragV = f(10),
                                   .still = boolArg(args, 11)}));
        return binding::none();
    };
}

// `SetupRadio(object, onPickUp, track, onSegment, djLine)`: the object made a radio and pinned.
// @orig 0x00379ea0 SetupRadio (unknown)
NativeFunction makeSetupRadio(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->radios == nullptr) {
            return binding::none();
        }
        const double object = handleArg(args, 0);
        if (context->spawnRecords != nullptr) {
            context->spawnRecords->setPinned(object, true);
        }
        context->radios->setup(object, nameArg(args, 1), static_cast<int>(wholeArg(args, 2)), nameArg(args, 3),
                               static_cast<int>(wholeArg(args, 4)));
        return binding::none();
    };
}

} // namespace

void addStoryEffectsBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle) {
    vm.registerFunction("CamAddPoizoPoint", makeCamAddPoizoPoint(context));
    vm.registerFunction("CamAddPoizoPointCam", makeCamAddPoizoPointCam(context));
    vm.registerFunction("CameraGetActive", makeCameraGetActive(context));
    vm.registerFunction("CameraSetClipping", makeCameraSetClipping(context));
    vm.registerFunction("CfgSteam", makeCfgSteam(context));
    vm.registerFunction("EndGarbage", makeGarbage(context, false));
    vm.registerFunction("MaxFogParticles", makeMaxFogParticles(context));
    vm.registerFunction("Start3DFog", makeStart3DFog(context));
    vm.registerFunction("StartGarbage", makeGarbage(context, true));
    vm.registerFunction("CamGetPos", makeCamGetPos(context));
    vm.registerFunction("CamSetFollowPos", makeCamSetFollowPos(context));
    vm.registerFunction("CamSetupPoizo", makeCamSetupPoizo(context, std::move(nextHandle)));
    vm.registerFunction("EndParticle", makeParticleSwitch(context, false));
    vm.registerFunction("SoundEnableEffects", makeSoundEnableEffects(context));
    vm.registerFunction("SoundEnableSystemMusic", makeSoundEnableSystemMusic(context));
    vm.registerFunction("SoundSetEffect", makeSoundSetEffect(context));
    vm.registerFunction("SoundSetMusicTrack", makeSoundSetMusicTrack(context));
    vm.registerFunction("SetupRadio", makeSetupRadio(context));
    vm.registerFunction("StartParticle", makeParticleSwitch(context, true));
}

} // namespace coney::script
