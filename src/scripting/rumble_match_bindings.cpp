// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/rumble_match_bindings.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "camera/cameras.h"
#include "camera/win_camera.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// The null handle: what `CameraCreateWin` returns when it makes nothing.
constexpr double kNilHandle = 0.0;
// How many names `ShowRumbleModeIntro`'s table holds.
constexpr std::size_t kIntroNames = 10;
// What `HuGetGang` returns for a handle that names no human.
constexpr double kNoGang = 65535;

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as a handle: truncated to an unsigned 32-bit integer.
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(static_cast<std::uint32_t>(wholeArg(args, i)));
}

// The bindings read the hosts at each call, not at registration: a level gives its brains to a Lua state made before
// it (gamemodes/gameplay_mode.h).

// Whether argument `i` is missing or nil, so its default applies.
bool absent(std::span<const Value> args, std::size_t i) { return i >= args.size() || args[i].isNil(); }

// Argument `i` as a float, or `fallback` when it is absent.
float floatOr(std::span<const Value> args, std::size_t i, float fallback) {
    return absent(args, i) ? fallback : static_cast<float>(binding::number(args, i));
}

// `ShowRumbleModeIntro(onDone, names)`: the callback's name and the strings at names[1..10] (a missing or non-string
// element is empty).
// @orig 0x0036ed48 ShowRumbleModeIntro (unknown)
NativeFunction makeShowRumbleModeIntro(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        BindingHost* host = context->host;
        std::vector<std::string> names;
        if (args.size() > 1 && args[1].table() != nullptr) {
            for (std::size_t k = 1; k <= kIntroNames; ++k) {
                const Value element = args[1].table()->get(Value(static_cast<double>(k)));
                names.emplace_back(element.string().value_or(""));
            }
        }
        if (host != nullptr) {
            host->showRumbleModeIntro(binding::string(args, 0), names);
        }
        return binding::none();
    };
}

// `HUDLaunchRumbleWin(winner, reason)`.
// @orig 0x0036f160 HUDLaunchRumbleWin (unknown)
NativeFunction makeHudLaunchRumbleWin(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        BindingHost* host = context->host;
        if (host != nullptr) {
            host->launchRumbleWin(binding::string(args, 0), binding::string(args, 1));
        }
        return binding::none();
    };
}

// `TacticAttack(gang, callback)`.
// @orig 0x00375370 TacticAttack (unknown)
NativeFunction makeTacticAttack(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        AiBindingHost* ai = context->ai;
        if (ai != nullptr) {
            ai->tacticAttack(static_cast<int>(wholeArg(args, 0)), binding::string(args, 1));
        }
        return binding::none();
    };
}

// `TacticConfront(gang, targetGang, approachRange, criticalRange, confrontation, slotSet, callback, anim1-5,
// spotLine)`: the defaults -1, 10, 2 and -1 for missing arguments.
// @orig 0x0030e670 TacticConfront_Init (unknown)
NativeFunction makeTacticConfront(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        AiBindingHost* ai = context->ai;
        if (ai != nullptr) {
            ai->tacticConfront(ConfrontCall{
                .gang = static_cast<int>(wholeArg(args, 0)),
                .targetGang = absent(args, 1) ? -1 : static_cast<int>(wholeArg(args, 1)),
                .approachRange = floatOr(args, 2, 10.0F),
                .criticalRange = floatOr(args, 3, 2.0F),
                .confrontation = static_cast<std::uint32_t>(wholeArg(args, 4)),
                .slotSet = absent(args, 5) ? -1 : static_cast<int>(wholeArg(args, 5)),
                .callback = binding::string(args, 6),
            });
        }
        return binding::none();
    };
}

// `BrFlushActions(human)`.
// @orig 0x0035ee18 BrFlushActions (unknown)
NativeFunction makeBrFlushActions(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        AiBindingHost* ai = context->ai;
        if (ai != nullptr) {
            ai->brFlushActions(handleArg(args, 0));
        }
        return binding::none();
    };
}

// `BrFlushGoals(human)`.
// @orig 0x0035eea0 BrFlushGoals (unknown)
NativeFunction makeBrFlushGoals(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        AiBindingHost* ai = context->ai;
        if (ai != nullptr) {
            ai->brFlushGoals(handleArg(args, 0));
        }
        return binding::none();
    };
}

// `HuSetMaxHealth(human, health)`: the health a 16-bit number.
// @orig 0x0035bbc8 HuSetMaxHealth (unknown)
NativeFunction makeHuSetMaxHealth(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        AiBindingHost* ai = context->ai;
        if (ai != nullptr) {
            ai->setMaxHealth(handleArg(args, 0), static_cast<std::int16_t>(wholeArg(args, 1)));
        }
        return binding::none();
    };
}

// `HuDelete(human)`.
// @orig 0x00358608 HuDelete (unknown)
NativeFunction makeHuDelete(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        AiBindingHost* ai = context->ai;
        if (ai != nullptr) {
            ai->humanDelete(handleArg(args, 0));
        }
        return binding::none();
    };
}

// `HuGetGang(human)`: the gang's id, 65535 when the handle names no human in a gang.
// @orig 0x0035b398 HuGetGang (unknown)
NativeFunction makeHuGetGang(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        AiBindingHost* ai = context->ai;
        const std::optional<int> gang = ai != nullptr ? ai->gangOf(handleArg(args, 0)) : std::nullopt;
        return binding::number(gang ? static_cast<double>(*gang) : kNoGang);
    };
}

// `CameraCreateWin(name, target, fov, distance, angle, speed, height, far, direction)`: the win camera set up on the
// target; its handle, or NilHandle when there are no cameras.
// @orig 0x00366360 CameraCreateWin (unknown)
NativeFunction makeCameraCreateWin(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        camera::Cameras* cameras = context->cameras;
        if (cameras == nullptr) {
            return binding::number(kNilHandle);
        }
        const camera::WinCameraSettings settings{
            .fieldOfView = static_cast<float>(binding::number(args, 2)),
            .distance = static_cast<float>(binding::number(args, 3)),
            .angleDegrees = static_cast<float>(binding::number(args, 4)),
            .speedDegrees = static_cast<float>(binding::number(args, 5)),
            .height = static_cast<float>(binding::number(args, 6)),
            .farClip = static_cast<float>(binding::number(args, 7)),
            .direction = (absent(args, 8) || wholeArg(args, 8) >= 1) ? 1.0F : -1.0F,
        };
        return binding::number(cameras->createWin(nextHandle(), handleArg(args, 1), settings));
    };
}

// `CamSetFollowHeading(degrees)`.
// @orig 0x00365b80 CamSetFollowHeading (unknown)
NativeFunction makeCamSetFollowHeading(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (camera::Cameras* cameras = context->cameras; cameras != nullptr) {
            cameras->setFollowHeading(static_cast<float>(binding::number(args, 0)));
        }
        return binding::none();
    };
}

// `CamDelete(camera)`.
// @orig 0x00365818 CamDelete (unknown)
NativeFunction makeCamDelete(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (camera::Cameras* cameras = context->cameras; cameras != nullptr) {
            cameras->deleteCamera(handleArg(args, 0));
        }
        return binding::none();
    };
}

} // namespace

void addRumbleMatchBindings(LuaVm& vm, const BindingContext& context, const std::function<double()>& nextHandle) {
    vm.registerFunction("CamDelete", makeCamDelete(context));
    vm.registerFunction("CamSetFollowHeading", makeCamSetFollowHeading(context));
    vm.registerFunction("CameraCreateWin", makeCameraCreateWin(context, nextHandle));
    vm.registerFunction("BrFlushActions", makeBrFlushActions(context));
    vm.registerFunction("BrFlushGoals", makeBrFlushGoals(context));
    vm.registerFunction("HUDLaunchRumbleWin", makeHudLaunchRumbleWin(context));
    vm.registerFunction("HuDelete", makeHuDelete(context));
    vm.registerFunction("HuGetGang", makeHuGetGang(context));
    vm.registerFunction("HuSetMaxHealth", makeHuSetMaxHealth(context));
    vm.registerFunction("ShowRumbleModeIntro", makeShowRumbleModeIntro(context));
    vm.registerFunction("TacticAttack", makeTacticAttack(context));
    vm.registerFunction("TacticConfront", makeTacticConfront(context));
}

} // namespace coney::script
