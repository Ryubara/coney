// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/gang_bindings.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"

namespace coney::script {

namespace {

// Argument `i` as an integer, truncated as tolua reads one.
int intArg(std::span<const Value> args, std::size_t i) {
    return static_cast<int>(std::trunc(binding::number(args, i)));
}

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

// A binding that hands its arguments to the host when there is one and returns nothing. The host is read at each call,
// not at registration: a level gives its brains to a Lua state made before it (gamemodes/gameplay_mode.h).
template <typename Body> NativeFunction hostCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        if (AiBindingHost* host = context->ai; host != nullptr) {
            body(*host, args);
        }
        return binding::none();
    };
}

// `GangAddMember(gang, index, human)`: the index is read and never used.
// @orig 0x00373528 GangAddMember (unknown)
NativeFunction makeGangAddMember(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.gangAddMember(intArg(args, 0), handleArg(args, 2));
    });
}

// `GangBrDead(gang, on)`.
// @orig 0x00373a20 GangBrDead (unknown)
NativeFunction makeGangBrDead(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.gangBrDead(intArg(args, 0), boolArg(args, 1));
    });
}

// `GangBrFlush(gang)`.
// @orig 0x0035f6f0 GangBrFlush (unknown)
NativeFunction makeGangBrFlush(const BindingContext& context) {
    return hostCall(context,
                    [](AiBindingHost& host, std::span<const Value> args) { host.gangBrFlush(intArg(args, 0)); });
}

// `GangDelete(gang)`.
// @orig 0x00373200 GangDelete (unknown)
NativeFunction makeGangDelete(const BindingContext& context) {
    return hostCall(context,
                    [](AiBindingHost& host, std::span<const Value> args) { host.gangDelete(intArg(args, 0)); });
}

// `GangGetHeadCount(gang, living) -> number`: 0 without a host.
// @orig 0x003735d0 GangGetHeadCount (unknown)
NativeFunction makeGangGetHeadCount(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        AiBindingHost* host = context->ai;
        return binding::number(host != nullptr ? host->gangHeadCount(intArg(args, 0), boolArg(args, 1)) : 0);
    };
}

// `GangGetStandingCount(gang) -> number`: 0 without a host.
// @orig 0x00373648 GangGetStandingCount (unknown)
NativeFunction makeGangGetStandingCount(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        AiBindingHost* host = context->ai;
        return binding::number(host != nullptr ? host->gangStandingCount(intArg(args, 0)) : 0);
    };
}

// `GangMakeEnemies(gang, other)`.
// @orig 0x00373b78 GangMakeEnemies (unknown)
NativeFunction makeGangMakeEnemies(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.gangMakeEnemies(intArg(args, 0), intArg(args, 1));
    });
}

// `GangMakeFriends(gang, other)`.
// @orig 0x00373bf0 GangMakeFriends (unknown)
NativeFunction makeGangMakeFriends(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.gangMakeFriends(intArg(args, 0), intArg(args, 1));
    });
}

// `GangSetMsgHandler(gang, message, handler)`: nil clears the handler.
// @orig 0x00373370 GangSetMsgHandler (unknown)
NativeFunction makeGangSetMsgHandler(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        const bool cleared = args.size() <= 2 || args[2].isNil();
        host.gangSetMsgHandler(intArg(args, 0), intArg(args, 1), cleared ? std::string{} : binding::string(args, 2));
    });
}

// `GangSetAttackable(gang, on)`: `on` left out is true.
// @orig 0x0035f790 GangSetAttackable (unknown)
NativeFunction makeGangSetAttackable(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.gangSetAttackable(intArg(args, 0), args.size() < 2 || boolArg(args, 1));
    });
}

// `GangSetThreatResponse(gang, response)`.
// @orig 0x0035f1f8 GangSetThreatResponse (unknown)
NativeFunction makeGangSetThreatResponse(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.gangSetThreatResponse(intArg(args, 0), intArg(args, 1));
    });
}

// `GangSuspend(gang, on)`.
// @orig 0x00373238 GangSuspend (unknown)
NativeFunction makeGangSuspend(const BindingContext& context) {
    return hostCall(context, [](AiBindingHost& host, std::span<const Value> args) {
        host.gangSuspend(intArg(args, 0), boolArg(args, 1));
    });
}

} // namespace

void addGangBindings(LuaVm& vm, const BindingContext& context) {
    vm.registerFunction("GangAddMember", makeGangAddMember(context));
    vm.registerFunction("GangBrDead", makeGangBrDead(context));
    vm.registerFunction("GangBrFlush", makeGangBrFlush(context));
    vm.registerFunction("GangDelete", makeGangDelete(context));
    vm.registerFunction("GangGetHeadCount", makeGangGetHeadCount(context));
    vm.registerFunction("GangGetStandingCount", makeGangGetStandingCount(context));
    vm.registerFunction("GangMakeEnemies", makeGangMakeEnemies(context));
    vm.registerFunction("GangMakeFriends", makeGangMakeFriends(context));
    vm.registerFunction("GangSetAttackable", makeGangSetAttackable(context));
    vm.registerFunction("GangSetMsgHandler", makeGangSetMsgHandler(context));
    vm.registerFunction("GangSetThreatResponse", makeGangSetThreatResponse(context));
    vm.registerFunction("GangSuspend", makeGangSuspend(context));
}

} // namespace coney::script
