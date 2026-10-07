// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/world_bindings.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/human_bindings.h"
#include "world_objects/flag_net.h"
#include "world_objects/level_objects.h"
#include "world_objects/object_tasks.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"
#include "world_objects/trigger_spheres.h"

namespace coney::script {

namespace {

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

// Argument `i` as a whole number where a script may pass a boolean (`TriggerSphereCfg`'s mode: true is 1).
int numberOrBooleanArg(std::span<const Value> args, std::size_t i) {
    if (i < args.size() && !args[i].number() && !args[i].isNil()) {
        return 1;
    }
    return static_cast<int>(std::trunc(binding::number(args, i)));
}

// Argument `i` as an unsigned 32-bit number, as tolua reads one (a negative number wraps).
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(std::trunc(binding::number(args, i))));
}

// `CfgSubtitles(enabled)`: the subtitle switch in the game state (`+0x438`).
// @orig 0x0041da30 Cfg_SetSubtitles (unknown)
NativeFunction makeCfgSubtitles(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->state != nullptr) {
            context->state->subtitles = booleanArg(args, 0, false);
        }
        return binding::none();
    };
}

// `DoorCRCCheck(a, b, c, d)` and `EnableShadow(object, enable)`: their callees return at once in this build.
// @orig 0x00397330 DoorCRCCheck (unknown)
// @orig 0x0017f270 Shadow_Enable_Stub (unknown)
NativeFunction makeNothing() {
    return [](std::span<const Value>) { return binding::none(); };
}

// `FlagNetAddLink(flag, link1, link2, link3, link4)`: a node of the flag network; ignored once it holds 128.
// @orig 0x002a7458 FlagNet_AddNode (unknown)
NativeFunction makeFlagNetAddLink(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->flagNet != nullptr) {
            static_cast<void>(context->flagNet->add(world_objects::FlagNetNode{
                .flag = handleArg(args, 0),
                .links = {handleArg(args, 1), handleArg(args, 2), handleArg(args, 3), handleArg(args, 4)}}));
        }
        return binding::none();
    };
}

// `FlagNetTraverse(human, mode, chance, flagA, flagB)`: the human wanders the network. The short form
// (`(human, flag, n)`, arguments 2-3 not numbers) starts it with the defaults; both reach the AI host.
// @orig 0x0037a968 FlagNetTraverse (unknown)
// @orig 0x0037a8d0 FlagNetTraverse_Short (unknown)
NativeFunction makeFlagNetTraverse(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->ai == nullptr) {
            return binding::none();
        }
        FlagNetTraverseCall call{.human = handleArg(args, 0)};
        const bool shortForm = args.size() > 1 && !args[1].number() && !args[1].isNil();
        if (!shortForm) {
            call.mode = static_cast<int>(std::trunc(binding::number(args, 1)));
            call.chance = static_cast<int>(unsignedArg(args, 2));
            call.flagA = booleanArg(args, 3, false);
            call.flagB = booleanArg(args, 4, false);
        }
        context->ai->flagNetTraverse(call);
        return binding::none();
    };
}

// `ObjDestroy(object, viaMessage)`: the object goes for good, a human holding it letting go first. Without
// `viaMessage` at once; with it the destroy message 0x15, which an objective marker handles by fading out before it
// goes (marked dying; world_objects::ObjectTasks removes it at alpha 0). Coney's other classes have no handling of
// their own, so they go at once (docs/research/objects.md#objective-markers).
// @orig 0x00396c58 Obj_Destroy (unknown)
NativeFunction makeObjDestroy(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const double object = handleArg(args, 0);
        // A pane is released: it vanishes, with no shatter (docs/research/objects.md#pane).
        if (world_objects::LevelObjects* objects = context->objects;
            objects != nullptr && objects->glass.release(object, objects->world)) {
            return binding::none();
        }
        if (context->ai != nullptr) {
            if (HumanBindingHost* humans = context->ai->humans(); humans != nullptr) {
                humans->releaseObject(object);
            }
        }
        if (context->spawnRecords == nullptr) {
            return binding::none();
        }
        world_objects::SpawnRecord* record = context->spawnRecords->find(object);
        const world_objects::ObjectType* type = record != nullptr && context->objectTypes != nullptr
                                                    ? context->objectTypes->find(record->typeName)
                                                    : nullptr;
        if (record != nullptr && !record->removed && type != nullptr && booleanArg(args, 1, false) &&
            type->className == world_objects::kObjectiveClass) {
            record->dying = true;
        } else {
            static_cast<void>(context->spawnRecords->destroy(object));
        }
        return binding::none();
    };
}

// `ObjEnableZone(zone, enable)`: the zone's bit of the spawn table's mask.
// @orig 0x00396778 ObjZone_Enable (unknown)
NativeFunction makeObjEnableZone(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->spawnRecords != nullptr) {
            context->spawnRecords->setZoneEnabled(unsignedArg(args, 0), booleanArg(args, 1, true));
        }
        return binding::none();
    };
}

// `ObjShow(object, fadeDist)` and `ObjHide(object)`: the show/hide message (0x0a) to the object; resolving the handle
// spawns a record's object first. The fade distance is kept in the record (`+0x138`).
// @orig 0x00396a08 Obj_Show (unknown)
// @orig 0x00396b68 Obj_Hide (unknown)
NativeFunction makeObjShowHide(const BindingContext& context, bool show) {
    return [context = &context, show](std::span<const Value> args) {
        if (context->spawnRecords == nullptr) {
            return binding::none();
        }
        if (world_objects::SpawnRecord* record = context->spawnRecords->resolve(handleArg(args, 0))) {
            record->hidden = !show;
            record->shownMessage = show;
            if (show && args.size() > 1 && args[1].number()) {
                record->fadeInDistance = static_cast<float>(binding::number(args, 1));
            }
        }
        return binding::none();
    };
}

// `TriggerSphereCfg(object, enable, radius, mode, interval)`: the object's trigger sphere, made or reconfigured.
// @orig 0x0036d0e0 TriggerSphereCfg (unknown)
// @orig 0x00414bc0 TriggerSphere_Configure (unknown)
NativeFunction makeTriggerSphereCfg(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->spheres != nullptr) {
            static_cast<void>(context->spheres->configure(handleArg(args, 0), booleanArg(args, 1, true),
                                                          static_cast<float>(binding::number(args, 2)),
                                                          numberOrBooleanArg(args, 3), unsignedArg(args, 4)));
        }
        return binding::none();
    };
}

// `TriggerSphereEnable(object, enable)`: the object's trigger sphere armed (made when it has none) or disarmed.
// @orig 0x0036d008 TriggerSphereEnable (unknown)
NativeFunction makeTriggerSphereEnable(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (context->spheres != nullptr) {
            static_cast<void>(context->spheres->arm(handleArg(args, 0), booleanArg(args, 1, false)));
        }
        return binding::none();
    };
}

} // namespace

void addWorldBindings(LuaVm& vm, const BindingContext& context) {
    vm.registerFunction("CfgSubtitles", makeCfgSubtitles(context));
    vm.registerFunction("DoorCRCCheck", makeNothing());
    vm.registerFunction("EnableShadow", makeNothing());
    vm.registerFunction("FlagNetAddLink", makeFlagNetAddLink(context));
    vm.registerFunction("FlagNetTraverse", makeFlagNetTraverse(context));
    vm.registerFunction("ObjDestroy", makeObjDestroy(context));
    vm.registerFunction("ObjEnableZone", makeObjEnableZone(context));
    vm.registerFunction("ObjHide", makeObjShowHide(context, false));
    vm.registerFunction("ObjShow", makeObjShowHide(context, true));
    vm.registerFunction("TriggerSphereCfg", makeTriggerSphereCfg(context));
    vm.registerFunction("TriggerSphereEnable", makeTriggerSphereEnable(context));
}

} // namespace coney::script
