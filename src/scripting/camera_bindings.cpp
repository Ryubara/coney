// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/camera_bindings.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "camera/cameras.h"
#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// The handle scripts read as no object.
constexpr double kNilHandle = 0.0;

// Argument `i` as an integer, truncated as tolua reads one.
int intArg(std::span<const Value> args, std::size_t i) {
    return static_cast<int>(std::trunc(binding::number(args, i)));
}

// Argument `i` as a float, as tolua reads a single-precision number.
float floatArg(std::span<const Value> args, std::size_t i) { return static_cast<float>(binding::number(args, i)); }

// Argument `i` as a boolean: nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].isNil()) {
        return false;
    }
    return binding::number(args, i) != 0.0 || args[i].type() != Value::Type::Number;
}

// A handle argument: truncated to an unsigned integer.
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(static_cast<std::uint32_t>(std::trunc(binding::number(args, i))));
}

// A binding that hands its arguments to the cameras when there are some and returns nothing. The cameras are read at
// each call: gameplay sets them per level, after the state's bindings were made.
template <typename Body> NativeFunction camerasCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        if (camera::Cameras* cameras = context->cameras; cameras != nullptr) {
            body(*cameras, args);
        }
        return binding::none();
    };
}

// `CamSetupFollow(name, target)`: the follow camera put on its target behind it; its handle (a new one the first
// time). Coney's follow camera is player 1's, whatever the target.
// @orig 0x00365a48 CamSetupFollow (unknown)
NativeFunction makeCamSetupFollow(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> /*args*/) {
        camera::Cameras* cameras = context->cameras;
        if (cameras == nullptr) {
            return binding::number(nextHandle());
        }
        const double handle = cameras->followHandle().value_or(0.0);
        return binding::number(cameras->setupFollow(handle != 0.0 ? handle : nextHandle()));
    };
}

// `CfgFollowCamera(min, max, default, pitchDegrees, fov, near, {offset}, slowmo)`.
// @orig 0x0036ab88 CfgFollowCamera (unknown)
NativeFunction makeCfgFollowCamera(const BindingContext& context) {
    return camerasCall(context, [](camera::Cameras& cameras, std::span<const Value> args) {
        camera::FollowSettings settings;
        settings.minDistance = floatArg(args, 0);
        settings.maxDistance = floatArg(args, 1);
        settings.defaultDistance = floatArg(args, 2);
        settings.pitchDegrees = floatArg(args, 3);
        settings.fieldOfView = floatArg(args, 4);
        settings.nearClip = floatArg(args, 5);
        const std::array<float, 3> offset = binding::position(args, 6).value_or(std::array<float, 3>{});
        settings.lookAtX = offset[0];
        settings.lookAtY = offset[1];
        settings.lookAtHeight = offset[2];
        cameras.configureFollow(settings, floatArg(args, 7));
    });
}

// `CameraCreateLocked(name, pos, fov, heading, pitch, roll, near, far)`: a new locked camera's handle.
// @orig 0x00365d38 CameraCreateLocked (unknown)
NativeFunction makeCameraCreateLocked(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        const double handle = nextHandle();
        if (camera::Cameras* cameras = context->cameras; cameras != nullptr) {
            const std::array<float, 3> p = binding::position(args, 1).value_or(std::array<float, 3>{});
            cameras->createLocked(handle, camera::LockedCamera{.position = anim::Vec3{p[0], p[1], p[2]},
                                                               .headingDegrees = floatArg(args, 3),
                                                               .pitchDegrees = floatArg(args, 4),
                                                               .rollDegrees = floatArg(args, 5),
                                                               .fieldOfView = floatArg(args, 2),
                                                               .nearClip = floatArg(args, 6),
                                                               .farClip = floatArg(args, 7),
                                                               .keptInView = {}});
        }
        return binding::number(handle);
    };
}

// `CamLockLocked(camera, human, on)`: the human added to or removed from the locked camera's kept-in-view list.
// @orig 0x00367030 CamLockLocked (unknown)
NativeFunction makeCamLockLocked(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->cameras != nullptr) {
            context->cameras->lockLocked(handleArg(args, 0), handleArg(args, 1), boolArg(args, 2));
        }
        return binding::none();
    };
}

// `CameraMakeActive(camera, seconds, name, forHuman, fromHuman)`: Coney has player 1's cameras only, so the two
// humans are not read.
// @orig 0x003656a0 CameraMakeActive (unknown)
NativeFunction makeCameraMakeActive(const BindingContext& context) {
    return camerasCall(context, [](camera::Cameras& cameras, std::span<const Value> args) {
        cameras.makeActive(handleArg(args, 0), floatArg(args, 1));
    });
}

// `CameraReset(camera)`.
// @orig 0x00365a10 CameraReset (unknown)
NativeFunction makeCameraReset(const BindingContext& context) {
    return camerasCall(
        context, [](camera::Cameras& cameras, std::span<const Value> args) { cameras.reset(handleArg(args, 0)); });
}

// `CamSetFollowZoom(preset, player)`: presets 0-2; other values do nothing.
// @orig 0x00365bf0 CamSetFollowZoom (unknown)
NativeFunction makeCamSetFollowZoom(const BindingContext& context) {
    return camerasCall(context, [](camera::Cameras& cameras, std::span<const Value> args) {
        const auto preset = static_cast<std::uint32_t>(std::trunc(binding::number(args, 0)));
        if (preset <= static_cast<std::uint32_t>(camera::FollowZoom::Far)) {
            cameras.setFollowZoom(static_cast<camera::FollowZoom>(preset));
        }
    });
}

// `CamSetFollowAngle(degrees)`.
// @orig 0x00365bb8 CamSetFollowAngle (unknown)
NativeFunction makeCamSetFollowAngle(const BindingContext& context) {
    return camerasCall(context, [](camera::Cameras& cameras, std::span<const Value> args) {
        cameras.setFollowAngle(floatArg(args, 0));
    });
}

// `CamSetSplitMode(mode)`.
// @orig 0x00367270 CamSetSplitMode (unknown)
NativeFunction makeCamSetSplitMode(const BindingContext& context) {
    return camerasCall(context, [](camera::Cameras& cameras, std::span<const Value> args) {
        cameras.setSplitMode(
            static_cast<std::uint32_t>(static_cast<std::int64_t>(std::trunc(binding::number(args, 0)))));
    });
}

// `CamAssignRevCamButton(button)`.
// @orig 0x003672a8 CamAssignRevCamButton (unknown)
NativeFunction makeCamAssignRevCamButton(const BindingContext& context) {
    return camerasCall(context, [](camera::Cameras& cameras, std::span<const Value> args) {
        cameras.setReverseButton(
            static_cast<std::uint32_t>(static_cast<std::int64_t>(std::trunc(binding::number(args, 0)))));
    });
}

// `CamSetSecondary(object, range, player)`: player 1's only (index 0, the default).
// @orig 0x003670c0 CamSetSecondary (unknown)
NativeFunction makeCamSetSecondary(const BindingContext& context) {
    return camerasCall(context, [](camera::Cameras& cameras, std::span<const Value> args) {
        if (intArg(args, 2) == 0) {
            cameras.setSecondary(handleArg(args, 0), floatArg(args, 1));
        }
    });
}

// `CamEnable(feature, on, player)`: the switch for every player (Coney has one).
// @orig 0x003671e0 CamEnable (unknown)
NativeFunction makeCamEnable(const BindingContext& context) {
    return camerasCall(context, [](camera::Cameras& cameras, std::span<const Value> args) {
        cameras.enable(static_cast<std::uint32_t>(std::trunc(binding::number(args, 0))), boolArg(args, 1));
    });
}

// `CamTarget(mode, camera, human)`: whether the shared list changed; the camera is read and not used.
// @orig 0x00365ad8 CamTarget (unknown)
NativeFunction makeCamTarget(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        camera::Cameras* cameras = context->cameras;
        if (cameras == nullptr) {
            return binding::boolean(false);
        }
        return binding::boolean(cameras->target(intArg(args, 0), handleArg(args, 2)));
    };
}

// Whether a `player` argument (default -1, every player) reaches player 1's cameras, the only ones Coney has.
bool forPlayerOne(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].isNil()) {
        return true;
    }
    const int player = intArg(args, i);
    return player == -1 || player == 0;
}

// A table argument {x, y, z} as a vector; the origin when it is missing.
anim::Vec3 vecArg(std::span<const Value> args, std::size_t i) {
    const std::array<float, 3> p = binding::position(args, i).value_or(std::array<float, 3>{});
    return anim::Vec3{p[0], p[1], p[2]};
}

// `CamSetupRail(name, target, fov, offset, near, far, player)`: the rail camera's handle, or NilHandle for no target.
// @orig 0x00366518 CamSetupRail (unknown)
NativeFunction makeCamSetupRail(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        camera::Cameras* cameras = context->cameras;
        const double target = handleArg(args, 1);
        if (cameras == nullptr || target == kNilHandle || !forPlayerOne(args, 6)) {
            return binding::number(kNilHandle);
        }
        camera::RailSetup setup;
        setup.fieldOfView = floatArg(args, 2);
        setup.offset = vecArg(args, 3);
        setup.nearClip = floatArg(args, 4);
        setup.farClip = floatArg(args, 5);
        const double handle = cameras->rail() != nullptr ? 0.0 : nextHandle();
        return binding::number(cameras->setupRail(handle, target, setup));
    };
}

// `CamAddRailPoint(pos, player)`: true when the point was added, nil without a rail camera.
// @orig 0x003667a0 CamAddRailPoint (unknown)
NativeFunction makeCamAddRailPoint(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        camera::Cameras* cameras = context->cameras;
        const bool added = cameras != nullptr && forPlayerOne(args, 1) && cameras->addRailPoint(vecArg(args, 0));
        return binding::boolean(added);
    };
}

// `CamLeadRail(lead, seconds, ahead, player)`: `ahead` defaults to true.
// @orig 0x003666e8 CamLeadRail (unknown)
NativeFunction makeCamLeadRail(const BindingContext& context) {
    return camerasCall(context, [](camera::Cameras& cameras, std::span<const Value> args) {
        if (forPlayerOne(args, 3)) {
            const bool ahead = args.size() <= 2 || boolArg(args, 2);
            cameras.leadRail(floatArg(args, 0), floatArg(args, 1), ahead);
        }
    });
}

// `CamModifyRail(param, value, seconds, player)`.
// @orig 0x003668e8 CamModifyRail (unknown)
NativeFunction makeCamModifyRail(const BindingContext& context) {
    return camerasCall(context, [](camera::Cameras& cameras, std::span<const Value> args) {
        if (forPlayerOne(args, 3)) {
            cameras.modifyRail(static_cast<std::uint32_t>(std::trunc(binding::number(args, 0))), floatArg(args, 1),
                               floatArg(args, 2));
        }
    });
}

// `CameraCreateFixed(name, target, pos, fov, offset, near, far)`: a new fixed camera's handle, or NilHandle for no
// target.
// @orig 0x00366140 CameraCreateFixed (unknown)
// @orig 0x0011c6b8 Camera_CreateFixed (unknown)
NativeFunction makeCameraCreateFixed(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        camera::Cameras* cameras = context->cameras;
        const double target = handleArg(args, 1);
        if (target == kNilHandle) {
            return binding::number(kNilHandle);
        }
        const double handle = nextHandle();
        if (cameras != nullptr) {
            cameras->createFixed(handle, target,
                                 camera::FixedCamera(vecArg(args, 2), vecArg(args, 4), floatArg(args, 3),
                                                     floatArg(args, 5), floatArg(args, 6)));
        }
        return binding::number(handle);
    };
}

// `CameraCreateThird(name, target, fov, distance, height, angle, offset, near, far)`: a new third-person camera's
// handle, or NilHandle for no target.
// @orig 0x00365f28 CameraCreateThird (unknown)
// @orig 0x0011be18 Camera_CreateThird (unknown)
NativeFunction makeCameraCreateThird(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        camera::Cameras* cameras = context->cameras;
        const double target = handleArg(args, 1);
        if (target == kNilHandle) {
            return binding::number(kNilHandle);
        }
        const double handle = nextHandle();
        if (cameras != nullptr) {
            camera::ThirdCameraSettings settings;
            settings.fieldOfView = floatArg(args, 2);
            settings.distance = floatArg(args, 3);
            settings.height = floatArg(args, 4);
            settings.angleDegrees = floatArg(args, 5);
            settings.offset = vecArg(args, 6);
            settings.nearClip = floatArg(args, 7);
            settings.farClip = floatArg(args, 8);
            cameras->createThird(handle, target, settings);
        }
        return binding::number(handle);
    };
}

} // namespace

void addCameraBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle) {
    vm.registerFunction("CamEnable", makeCamEnable(context));
    vm.registerFunction("CameraCreateLocked", makeCameraCreateLocked(context, nextHandle));
    vm.registerFunction("CameraMakeActive", makeCameraMakeActive(context));
    vm.registerFunction("CameraReset", makeCameraReset(context));
    vm.registerFunction("CamLockLocked", makeCamLockLocked(context));
    vm.registerFunction("CamSetFollowAngle", makeCamSetFollowAngle(context));
    vm.registerFunction("CamSetFollowZoom", makeCamSetFollowZoom(context));
    vm.registerFunction("CamSetSplitMode", makeCamSetSplitMode(context));
    vm.registerFunction("CamAssignRevCamButton", makeCamAssignRevCamButton(context));
    vm.registerFunction("CamSetSecondary", makeCamSetSecondary(context));
    vm.registerFunction("CamSetupRail", makeCamSetupRail(context, nextHandle));
    vm.registerFunction("CamAddRailPoint", makeCamAddRailPoint(context));
    vm.registerFunction("CamLeadRail", makeCamLeadRail(context));
    vm.registerFunction("CamModifyRail", makeCamModifyRail(context));
    vm.registerFunction("CameraCreateFixed", makeCameraCreateFixed(context, nextHandle));
    vm.registerFunction("CameraCreateThird", makeCameraCreateThird(context, nextHandle));
    vm.registerFunction("CamSetupFollow", makeCamSetupFollow(context, std::move(nextHandle)));
    vm.registerFunction("CamTarget", makeCamTarget(context));
    vm.registerFunction("CfgFollowCamera", makeCfgFollowCamera(context));
}

} // namespace coney::script
