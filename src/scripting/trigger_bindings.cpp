// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/trigger_bindings.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

#include "scripting/binding_args.h"
#include "scripting/message_handlers.h"
#include "world_objects/volume_boxes.h"

namespace coney::script {

namespace {

// The value of `NilHandle` (script_bindings.cpp sets the global).
constexpr double kNilHandle = 0.0;
// The box kind `AddVolumeBox` refuses.
constexpr int kRefusedKind = 1;

// Argument `i` as a handle: truncated to a whole number, as tolua reads an unsigned integer.
double handleArg(std::span<const Value> args, std::size_t i) { return std::trunc(binding::number(args, i)); }

// Argument `i` as tolua reads a boolean with a default: absent is the default, nil or false (and 0) false.
bool booleanArg(std::span<const Value> args, std::size_t i, bool fallback) {
    if (i >= args.size()) {
        return fallback;
    }
    if (const std::optional<double> value = args[i].number()) {
        return *value != 0.0;
    }
    return !args[i].isNil();
}

// `AddVolumeBox(name, kind, corner, size, enable, flags)`: a box from `corner` to `corner + size` with a new handle;
// NilHandle for kind 1. The flags byte is not kept (never passed, meaning not traced).
// @orig 0x004125b8 VolumeBox_Add (unknown)
NativeFunction makeAddVolumeBox(const BindingContext& context, std::function<double()> nextHandle) {
    return [boxes = context.boxes, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        const int kind = static_cast<int>(std::trunc(binding::number(args, 1)));
        if (kind == kRefusedKind) {
            return binding::number(kNilHandle);
        }
        const double handle = nextHandle();
        if (boxes != nullptr) {
            // tolua reads a missing corner or size as zeros.
            const std::array<float, 3> corner = binding::position(args, 2).value_or(std::array<float, 3>{});
            const std::array<float, 3> size = binding::position(args, 3).value_or(std::array<float, 3>{});
            boxes->add(handle, binding::string(args, 0), kind, corner, size, booleanArg(args, 4, true));
        }
        return binding::number(handle);
    };
}

// `RotateVolumeBox(box, m00, m01, m10, m11)`: the box's turn about the vertical axis.
// @orig 0x00412c40 VolumeBox_SetRotation (unknown)
NativeFunction makeRotateVolumeBox(const BindingContext& context) {
    return [boxes = context.boxes](std::span<const Value> args) {
        if (boxes != nullptr) {
            boxes->rotate(handleArg(args, 0),
                          {static_cast<float>(binding::number(args, 1)), static_cast<float>(binding::number(args, 2)),
                           static_cast<float>(binding::number(args, 3)), static_cast<float>(binding::number(args, 4))});
        }
        return binding::none();
    };
}

// `SetMsgHandler(object, message, callback)`: the callback for one message of the object.
// @orig 0x00386298 SetMsgHandler (unknown)
NativeFunction makeSetMsgHandler(const BindingContext& context) {
    return [messages = context.messages](std::span<const Value> args) {
        if (messages != nullptr) {
            messages->set(handleArg(args, 0), static_cast<int>(std::trunc(binding::number(args, 1))),
                          binding::string(args, 2));
        }
        return binding::none();
    };
}

// `SetMsgHandlerEx(object, message, callback, prompt, prompt2)`: SetMsgHandler, and for message 0 the object's
// interaction prompt (a kind-1 context record), registered with a callback and a prompt and dropped without either.
// The second text is the hint the HUD queues while the record is in reach (docs/research/hud.md#action-prompts).
// @orig 0x00386168 SetMsgHandlerEx (unknown)
NativeFunction makeSetMsgHandlerEx(const BindingContext& context) {
    return [messages = context.messages](std::span<const Value> args) {
        if (messages != nullptr) {
            const double object = handleArg(args, 0);
            const int message = static_cast<int>(std::trunc(binding::number(args, 1)));
            messages->set(object, message, binding::string(args, 2));
            if (message == 0) {
                messages->setPrompt(object, binding::string(args, 2), binding::string(args, 3),
                                    binding::string(args, 4));
            }
        }
        return binding::none();
    };
}

} // namespace

void addTriggerBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle) {
    vm.registerFunction("AddVolumeBox", makeAddVolumeBox(context, std::move(nextHandle)));
    vm.registerFunction("RotateVolumeBox", makeRotateVolumeBox(context));
    vm.registerFunction("SetMsgHandler", makeSetMsgHandler(context));
    vm.registerFunction("SetMsgHandlerEx", makeSetMsgHandlerEx(context));
}

} // namespace coney::script
