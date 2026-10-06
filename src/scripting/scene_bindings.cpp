// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/scene_bindings.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>

#include "scenes/scene_player.h"
#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// Argument `i` truncated to an unsigned integer, as tolua reads an `unsigned` (a scene id, a handle, a role).
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    const double value = std::trunc(binding::number(args, i));
    return value > 0.0 ? static_cast<std::uint32_t>(value) : 0U;
}

// Argument `i` as a handle: an unsigned integer, kept as the number the scripts hold.
double handleArg(std::span<const Value> args, std::size_t i) { return static_cast<double>(unsignedArg(args, i)); }

// Argument `i` as a float.
float floatArg(std::span<const Value> args, std::size_t i) { return static_cast<float>(binding::number(args, i)); }

// Argument `i` as tolua's boolean reader (0x00409318): nil and 0 are false, an absent argument too.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].isNil()) {
        return false;
    }
    return args[i].type() != Value::Type::Number || binding::number(args, i) != 0.0;
}

// Argument `i` as a boolean that is `fallback` only when the argument is absent: an explicit nil is false.
bool boolArgOr(std::span<const Value> args, std::size_t i, bool fallback) {
    return i >= args.size() ? fallback : boolArg(args, i);
}

// A function name argument: empty for nil (no function).
std::string nameArg(std::span<const Value> args, std::size_t i) {
    return i >= args.size() || args[i].isNil() ? std::string{} : binding::string(args, i);
}

// A binding that works on the scene system when there is one; `none` is its result without one.
template <typename Body> NativeFunction withScenes(const BindingContext& context, Body body) {
    return [scenes = context.scenes, body](std::span<const Value> args) -> binding::Results {
        if (scenes == nullptr) {
            return binding::none();
        }
        return body(*scenes, args);
    };
}

// `ScenePreload(name, onLoaded)`: the id; the callback is called with it when the record arrives. Without a scene
// system, 0 (the first scene), as for an unknown name.
// @orig 0x00367448 ScenePreload (unknown)
NativeFunction makeScenePreload(const BindingContext& context) {
    return [scenes = context.scenes](std::span<const Value> args) {
        if (scenes == nullptr) {
            return binding::number(0.0);
        }
        return binding::number(scenes->preload(binding::string(args, 0), nameArg(args, 1)));
    };
}

// `SceneIsPreloaded(name)`.
// @orig 0x003674d0 SceneIsPreloaded (unknown)
NativeFunction makeSceneIsPreloaded(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        return binding::boolean(scenes.isPreloaded(binding::string(args, 0)));
    });
}

// `SceneUnload(id)`.
// @orig 0x00367518 SceneUnload (unknown)
NativeFunction makeSceneUnload(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        scenes.unload(unsignedArg(args, 0));
        return binding::none();
    });
}

// `SceneSetCallback(name)`: nil or "" clears it.
// @orig 0x00367550 SceneSetCallback (unknown)
NativeFunction makeSceneSetCallback(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        scenes.setGlobalCallback(nameArg(args, 0));
        return binding::none();
    });
}

// `ScenePlayCinematic(id, delay, onEnd, bars, skippable, looping, freeze, blendCam, final, chain)`: at the origin with
// no rotation; freeze is true only when omitted.
// @orig 0x00367580 ScenePlayCinematic (unknown)
// @orig 0x00353c68 Scene_PlayCinematic (SceneCache.cpp)
NativeFunction makeScenePlayCinematic(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        const scenes::PlayRequest request{.kind = scenes::PlayKind::Cinematic,
                                          .delay = unsignedArg(args, 1),
                                          .onEnd = nameArg(args, 2),
                                          .cinematic = boolArg(args, 3),
                                          .skippable = boolArg(args, 4),
                                          .looping = boolArg(args, 5),
                                          .freeze = boolArgOr(args, 6, true),
                                          .final = boolArg(args, 8),
                                          .chain = boolArg(args, 9),
                                          .blendCam = floatArg(args, 7),
                                          .place = {}};
        return binding::boolean(scenes.play(unsignedArg(args, 0), request));
    });
}

// `ScenePlayFixedScene(id, delay, onEnd, looping, freeze, blendCam)`: not a cinematic and not skippable.
// @orig 0x00367708 ScenePlayFixedScene (unknown)
// @orig 0x00353d60 Scene_PlayFixed (SceneCache.cpp)
NativeFunction makeScenePlayFixedScene(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        const scenes::PlayRequest request{.kind = scenes::PlayKind::Fixed,
                                          .delay = unsignedArg(args, 1),
                                          .onEnd = nameArg(args, 2),
                                          .looping = boolArg(args, 3),
                                          .freeze = boolArg(args, 4),
                                          .blendCam = floatArg(args, 5),
                                          .place = {}};
        return binding::boolean(scenes.play(unsignedArg(args, 0), request));
    });
}

// `ScenePlay(id, {x, y, z}, heading | {i, j, k, r}, delay, onEnd, looping, freeze, blendCam)`: at the script's
// position and orientation (a heading in degrees about z, or a quaternion); freeze true when omitted. The flags'
// meanings are inferred.
// @orig 0x00367a20 ScenePlay (unknown)
// @orig 0x00367810 ScenePlay (unknown)
NativeFunction makeScenePlay(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        scenes::PlayRequest request{.kind = scenes::PlayKind::Placed,
                                    .delay = unsignedArg(args, 3),
                                    .onEnd = nameArg(args, 4),
                                    .looping = boolArg(args, 5),
                                    .freeze = boolArgOr(args, 6, true),
                                    .blendCam = floatArg(args, 7),
                                    .place = {}};
        if (const auto position = binding::position(args, 1)) {
            request.place.position = anim::Vec3{(*position)[0], (*position)[1], (*position)[2]};
        }
        if (args.size() > 2 && args[2].table() != nullptr) {
            const Table& rotation = *args[2].table();
            const auto component = [&rotation](double key) {
                return static_cast<float>(rotation.get(Value(key)).number().value_or(0.0));
            };
            request.place.rotation =
                anim::normalise(anim::Quat{component(1), component(2), component(3), component(4)});
        } else {
            const float half = floatArg(args, 2) * std::numbers::pi_v<float> / 360.0F;
            request.place.rotation = anim::Quat{0.0F, 0.0F, std::sin(half), std::cos(half)};
        }
        return binding::boolean(scenes.play(unsignedArg(args, 0), request));
    });
}

// `ScenePlayAnimation(id, onEnd, looping, flag, delay, flag2, freeze, blendCam)`. **Coney's reading**: which argument
// lands in which flag is not traced; `looping` and `freeze` are taken by name and the scene is played where it is
// authored (the fit to its humans, 0x003547e8, is not traced either).
// @orig 0x00367cd8 ScenePlayAnimation (unknown)
// @orig 0x00353f40 Scene_PlayAnimation (SceneCache.cpp)
NativeFunction makeScenePlayAnimation(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        const scenes::PlayRequest request{.kind = scenes::PlayKind::Animation,
                                          .delay = unsignedArg(args, 4),
                                          .onEnd = nameArg(args, 1),
                                          .looping = boolArg(args, 2),
                                          .freeze = boolArg(args, 6),
                                          .blendCam = floatArg(args, 7),
                                          .place = {}};
        return binding::boolean(scenes.play(unsignedArg(args, 0), request));
    });
}

// `SceneStop(id, force)`.
// @orig 0x00367e20 SceneStop (unknown)
NativeFunction makeSceneStop(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        scenes.stop(unsignedArg(args, 0), boolArg(args, 1));
        return binding::none();
    });
}

// `SceneTerminate(id)`: SceneStop(id, false).
// @orig 0x00368240 SceneTerminate (unknown)
NativeFunction makeSceneTerminate(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        scenes.stop(unsignedArg(args, 0), false);
        return binding::none();
    });
}

// `SceneDone(id)`.
// @orig 0x00367e80 SceneDone (unknown)
NativeFunction makeSceneDone(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        return binding::boolean(scenes.done(unsignedArg(args, 0)));
    });
}

// `SceneLength(id)`: the first part's length in milliseconds.
// @orig 0x00367ed0 SceneLength (unknown)
NativeFunction makeSceneLength(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        return binding::number(std::round(static_cast<double>(scenes.length(unsignedArg(args, 0))) * 1000.0));
    });
}

// `SceneAddObject(id, object, slot)`.
// @orig 0x00367f48 SceneAddObject (unknown)
NativeFunction makeSceneAddObject(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        scenes.addObject(unsignedArg(args, 0), handleArg(args, 1), unsignedArg(args, 2));
        return binding::none();
    });
}

// `GoalJoinCinematic(human, scene, role, gait, processNow)` and its fixed-scene and animation forms: the human is
// written into the role at once (0x003541a0); the goal's walk to the mark is the host's.
// @orig 0x003614f8 GoalJoinCinematic (unknown)
// @orig 0x00361418 GoalJoinFixedScene (unknown)
// @orig 0x003615d8 GoalJoinAnimation (unknown)
NativeFunction makeGoalJoin(const BindingContext& context) {
    return withScenes(context, [](scenes::SceneSystem& scenes, std::span<const Value> args) {
        scenes.joinHuman(handleArg(args, 0), unsignedArg(args, 1), unsignedArg(args, 2),
                         static_cast<int>(std::trunc(binding::number(args, 3))));
        return binding::none();
    });
}

} // namespace

void addSceneBindings(LuaVm& vm, const BindingContext& context) {
    vm.registerFunction("ScenePreload", makeScenePreload(context));
    vm.registerFunction("SceneIsPreloaded", makeSceneIsPreloaded(context));
    vm.registerFunction("SceneUnload", makeSceneUnload(context));
    vm.registerFunction("SceneSetCallback", makeSceneSetCallback(context));
    vm.registerFunction("ScenePlayCinematic", makeScenePlayCinematic(context));
    vm.registerFunction("ScenePlayFixedScene", makeScenePlayFixedScene(context));
    vm.registerFunction("ScenePlay", makeScenePlay(context));
    vm.registerFunction("ScenePlayAnimation", makeScenePlayAnimation(context));
    vm.registerFunction("SceneStop", makeSceneStop(context));
    vm.registerFunction("SceneTerminate", makeSceneTerminate(context));
    vm.registerFunction("SceneDone", makeSceneDone(context));
    vm.registerFunction("SceneLength", makeSceneLength(context));
    vm.registerFunction("SceneAddObject", makeSceneAddObject(context));
    vm.registerFunction("GoalJoinCinematic", makeGoalJoin(context));
    vm.registerFunction("GoalJoinFixedScene", makeGoalJoin(context));
    vm.registerFunction("GoalJoinAnimation", makeGoalJoin(context));
}

} // namespace coney::script
