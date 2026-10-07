// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/story_bindings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "camera/cameras.h"
#include "human/human_flags.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/human_bindings.h"
#include "warriors/crime_reports.h"
#include "world_objects/object_types.h"
#include "world_objects/volume_boxes.h"

namespace coney::script {

namespace {

// Coney's NilHandle (scripting/script_bindings.cpp), and the handle tolua gives an omitted handle argument whose
// default is the original's NilHandle (4294967295).
constexpr double kNilHandle = 0.0;
constexpr double kOmittedHandle = 4294967295.0;
// `WalkingDistance`'s answer when there is no route.
constexpr double kNoWalk = -1000000000.0;
// The longest callback name the game state keeps (`WCSetCallback`: 32-byte buffer).
constexpr std::size_t kCallbackLength = 31;
// The Warrior commands (0 follow ... 6 none).
constexpr int kWarriorCommandCount = static_cast<int>(kWarriorCommands);
// `HuTagPattern`'s table: 256 numbers read, the first 128 (64 points) kept.
constexpr std::size_t kTagPatternRead = 256;

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Argument `i` as an integer.
int intArg(std::span<const Value> args, std::size_t i) { return static_cast<int>(wholeArg(args, i)); }

// Argument `i` as an integer, `fallback` when omitted.
int intArgOr(std::span<const Value> args, std::size_t i, int fallback) {
    return i >= args.size() ? fallback : intArg(args, i);
}

// Argument `i` as an unsigned integer, `fallback` when omitted.
std::uint32_t unsignedArgOr(std::span<const Value> args, std::size_t i, std::uint32_t fallback) {
    return i >= args.size() ? fallback : static_cast<std::uint32_t>(wholeArg(args, i));
}

// Argument `i` as a float, `fallback` when omitted.
float floatArgOr(std::span<const Value> args, std::size_t i, float fallback) {
    return i >= args.size() ? fallback : static_cast<float>(binding::number(args, i));
}

// Argument `i` as a float.
float floatArg(std::span<const Value> args, std::size_t i) { return static_cast<float>(binding::number(args, i)); }

// Whether argument `i` is missing or nil.
bool absent(std::span<const Value> args, std::size_t i) { return i >= args.size() || args[i].isNil(); }

// Argument `i` as a boolean: nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (absent(args, i)) {
        return false;
    }
    return binding::number(args, i) != 0.0 || args[i].type() != Value::Type::Number;
}

// Argument `i` as a boolean that is `fallback` when omitted; a nil passed (Lua 4's false) is false, as tolua reads it.
bool boolArgOr(std::span<const Value> args, std::size_t i, bool fallback) {
    return i >= args.size() ? fallback : boolArg(args, i);
}

// Argument `i` as the switches that set a bit only for exactly true (1).
bool exactlyOne(std::span<const Value> args, std::size_t i) {
    if (absent(args, i)) {
        return false;
    }
    return args[i].type() == Value::Type::Number ? binding::number(args, i) == 1.0 : boolArg(args, i);
}

// A handle argument: truncated to an unsigned integer.
double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(static_cast<std::uint32_t>(wholeArg(args, i)));
}

// A handle argument whose omission (or the original's NilHandle) means none: 0.
double optionalHandleArg(std::span<const Value> args, std::size_t i) {
    if (absent(args, i)) {
        return kNilHandle;
    }
    const double handle = handleArg(args, i);
    return handle == kOmittedHandle ? kNilHandle : handle;
}

// A string argument that is empty for nil (a callback or anim name), cut to `length` characters.
std::string nameArg(std::span<const Value> args, std::size_t i, std::size_t length = std::string::npos) {
    std::string name = absent(args, i) ? std::string{} : binding::string(args, i);
    if (name.size() > length) {
        name.resize(length);
    }
    return name;
}

// Numbers 1..n of the table at argument `i`, 0 for a missing one; all 0 when it is not a table.
template <std::size_t N> std::array<double, N> tableArg(std::span<const Value> args, std::size_t i) {
    std::array<double, N> values{};
    if (i >= args.size() || args[i].table() == nullptr) {
        return values;
    }
    const Table& table = *args[i].table();
    for (std::size_t k = 0; k < N; ++k) {
        values.at(k) = table.get(Value(static_cast<double>(k + 1))).number().value_or(0.0);
    }
    return values;
}

// The numbers of a table as 16-bit object type ids (the tactic keeps them as 16 bits).
std::array<std::uint16_t, 8> objectTypes(const std::array<double, 8>& values) {
    std::array<std::uint16_t, 8> types{};
    for (std::size_t k = 0; k < values.size(); ++k) {
        types.at(k) = static_cast<std::uint16_t>(static_cast<std::int64_t>(values.at(k)));
    }
    return types;
}

// The numbers of a three-entry table as whole numbers (the tactic keeps them as bytes).
std::array<int, 3> tableInts(const std::array<double, 3>& values) {
    return {static_cast<int>(values[0]) & 0xff, static_cast<int>(values[1]) & 0xff, static_cast<int>(values[2]) & 0xff};
}

// The story host of the level, if there is a level with an AI host.
StoryBindingHost* storyOf(const BindingContext& context) {
    return context.ai != nullptr ? context.ai->story() : nullptr;
}

// The first-mission character host of the level (the flag switches), if any.
HumanBindingHost* humansOf(const BindingContext& context) {
    return context.ai != nullptr ? context.ai->humans() : nullptr;
}

// A binding that hands its arguments to the story host when there is one and returns nothing. The host is read at
// each call, not at registration: a level gives its brains to a Lua state made before it (gamemodes/gameplay_mode.h).
template <typename Body> NativeFunction storyCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        if (StoryBindingHost* host = storyOf(*context); host != nullptr) {
            body(*host, args);
        }
        return binding::none();
    };
}

// A binding that sets (`on`, read by `read`) or clears `bits` of the human named by argument 0, through the first
// mission's character host.
template <typename Read> NativeFunction flagCall(const BindingContext& context, std::uint64_t bits, Read read) {
    return [context = &context, bits, read](std::span<const Value> args) {
        if (HumanBindingHost* host = humansOf(*context); host != nullptr) {
            host->setFlags(handleArg(args, 0), bits, read(args, 1));
        }
        return binding::none();
    };
}

// A configuration binding: changes the game state's story fields with `body`.
template <typename Body> NativeFunction stateCall(const BindingContext& context, Body body) {
    return [context = &context, body](std::span<const Value> args) {
        body(context->state->story, args);
        return binding::none();
    };
}

// The squared distance between two points.
float squaredDistance(const StoryPoint& a, const StoryPoint& b) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return (dx * dx) + (dy * dy) + (dz * dz);
}

// Where the object `handle` names is, through the story host; nothing without one or such an object.
std::optional<StoryPoint> positionOf(const BindingContext& context, double handle) {
    const StoryBindingHost* host = storyOf(context);
    return host != nullptr ? host->position(handle) : std::nullopt;
}

// Which player (0 or 1) controls the human `handle` names; nothing for none.
std::optional<std::size_t> playerOf(const BindingContext& context, double handle) {
    const HumanBindingHost* host = humansOf(context);
    const std::optional<int> index = host != nullptr ? host->playerIndex(handle) : std::nullopt;
    if (!index || *index < 0 || *index >= static_cast<int>(kStoryPlayers)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*index);
}

// ---- The Warrior commands ----

// The dispatcher every Warrior command goes through (the menu's, `WCIssueCommand`'s and `IssueWarriorCommand`'s):
// nothing while a scene camera is player 1's, the commands are locked, the player's menu is locked, the command is
// disabled for the player or the human is no war chief; otherwise the command is the player's last, an unforced repeat
// of the current one only repeats its line, and any other starts the crew's tactic, then the `WCSetCallback` function
// is called with (chief, command). **Coney choices**: a command outside 0-6 is refused (the original does not check
// it), and the chief's line is not said (the speech commands of the lines are not on the page).
// @orig 0x0041c4e0 GameState_DispatchWarriorCommand (unknown)
void dispatchWarriorCommand(ScriptSystem& scripts, const BindingContext& context, double chief, int command,
                            bool forced) {
    StoryBindingHost* host = storyOf(context);
    CharacterRules& rules = context.state->characters;
    StoryState& story = context.state->story;
    if (host == nullptr || rules.warriorCommandsLocked || command < 0 || command >= kWarriorCommandCount) {
        return;
    }
    if (context.cameras != nullptr && context.cameras->current().kind == camera::CameraKind::Scene) {
        return;
    }
    const std::optional<std::size_t> player = playerOf(context, chief);
    if (!player || story.menuLocked.at(*player) ||
        !rules.warriorCommands.at(*player).at(static_cast<std::size_t>(command))) {
        return;
    }
    const bool repeat = rules.lastWarriorCommand.at(*player) == command;
    rules.lastWarriorCommand.at(*player) = command;
    if (repeat && !forced) {
        return;
    }
    if (!host->startWarriorCommand(chief, command, forced)) {
        return;
    }
    if (!story.commandCallback.empty()) {
        const std::array<Value, 2> args{Value(chief), Value(static_cast<double>(command))};
        scripts.call(story.commandCallback, args);
    }
}

// `IssueWarriorCommand(command, forced)`: as player 1's war chief.
// @orig 0x0041c2d0 GameState_IssueWarriorCommand (unknown)
NativeFunction makeIssueWarriorCommand(ScriptSystem& scripts, const BindingContext& context) {
    return [scripts = &scripts, context = &context](std::span<const Value> args) {
        if (const StoryBindingHost* host = storyOf(*context); host != nullptr) {
            dispatchWarriorCommand(*scripts, *context, host->playerOne(), intArg(args, 0), boolArg(args, 1));
        }
        return binding::none();
    };
}

// `WCEnableCommand(player, command, on)`: the command's switch for the player who controls `player`. **Coney
// choice**: a human no player controls or a command outside 0-6 does nothing (the original writes outside the table).
// @orig 0x0041dbe8 GameState_EnableWarriorCommand (unknown)
NativeFunction makeWcEnableCommand(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const std::optional<std::size_t> player = playerOf(*context, handleArg(args, 0));
        const int command = intArg(args, 1);
        if (player && command >= 0 && command < kWarriorCommandCount) {
            context->state->characters.warriorCommands.at(*player).at(static_cast<std::size_t>(command)) =
                boolArg(args, 2);
        }
        return binding::none();
    };
}

// `WCLockCommands(player, locked)`: whether the crew of the player who controls `player` turns on a chief who keeps
// hitting them (the byte's only reader, the Warrior brain's retaliation, is not built: kept for it).
// @orig 0x0041dcf0 GameState_LockWarriorCommands (unknown)
NativeFunction makeWcLockCommands(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (const std::optional<std::size_t> player = playerOf(*context, handleArg(args, 0)); player) {
            context->state->story.noRetaliation.at(*player) = boolArg(args, 1);
        }
        return binding::none();
    };
}

// `WCSetCallback(callback)`: nil clears it; 31 characters are kept.
// @orig 0x0041dd40 GameState_SetWarriorCommandCallback (unknown)
NativeFunction makeWcSetCallback(const BindingContext& context) {
    return stateCall(context, [](StoryState& story, std::span<const Value> args) {
        story.commandCallback = nameArg(args, 0, kCallbackLength);
    });
}

// `HUDShowWarCommand(on, player)`: whether the command display may open on that player's HUD.
// @orig 0x001b4948 HUD_ShowWarCommand (unknown)
NativeFunction makeHudShowWarCommand(const BindingContext& context) {
    return stateCall(context, [](StoryState& story, std::span<const Value> args) {
        const std::uint32_t player = unsignedArgOr(args, 1, 0);
        if (player < kStoryPlayers) {
            story.commandDisplay.at(player) = boolArgOr(args, 0, true);
        }
    });
}

// ---- Distances, paths and boxes ----

// `GetDistanceTweenHumans(a, b) -> number`: the straight distance between two objects. **Coney choice**: 0 when either
// handle names nothing (the original reads through a bad handle).
// @orig 0x002fee38 Objects_GetDistance (unknown)
NativeFunction makeGetDistanceTweenHumans(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const std::optional<StoryPoint> a = positionOf(*context, handleArg(args, 0));
        const std::optional<StoryPoint> b = positionOf(*context, handleArg(args, 1));
        return binding::number(a && b ? std::sqrt(squaredDistance(*a, *b)) : 0.0);
    };
}

// `TestDistance(a, b, distance) -> boolean`: closer than `distance` (exactly at it is not); false for a bad handle.
// @orig 0x00385ea8 Objects_TestDistance (unknown)
NativeFunction makeTestDistance(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const std::optional<StoryPoint> a = positionOf(*context, handleArg(args, 0));
        const std::optional<StoryPoint> b = positionOf(*context, handleArg(args, 1));
        const float limit = floatArg(args, 2);
        return binding::boolean(a && b && squaredDistance(*a, *b) < limit * limit);
    };
}

// `WalkingDistance(from, to) -> number`: over the level's routes; -1000000000 when there is none.
// @orig 0x00385f60 WalkingDistance (unknown)
// @orig 0x0024e478 Nav_GetWalkingDistance (unknown)
NativeFunction makeWalkingDistance(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        StoryBindingHost* host = storyOf(*context);
        const std::optional<float> walk =
            host != nullptr ? host->walkingDistance(handleArg(args, 0), handleArg(args, 1)) : std::nullopt;
        return binding::number(walk ? static_cast<double>(*walk) : kNoWalk);
    };
}

// `PathValid(from, to) -> boolean`: a walkable route joins the two objects.
// @orig 0x00386010 Obj_PathExists (unknown)
NativeFunction makePathValid(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        StoryBindingHost* host = storyOf(*context);
        return binding::boolean(host != nullptr &&
                                host->walkingDistance(handleArg(args, 0), handleArg(args, 1)).has_value());
    };
}

// `AddPath(name, {f1, ..., f8}) -> path`: a path through up to eight flags, or nil when the 32 slots are taken. **Coney
// stand-in**: Coney's VM has no user types, so the path is a number handle the path bindings take back.
// @orig 0x00415740 Path_Add (unknown)
NativeFunction makeAddPath(const BindingContext& context, std::function<double()> nextHandle) {
    return [context = &context, nextHandle = std::move(nextHandle)](std::span<const Value> args) {
        StoryBindingHost* host = storyOf(*context);
        if (host == nullptr) {
            return std::vector<Value>{Value()};
        }
        std::array<double, 8> points{};
        const std::array<double, 8> read = tableArg<8>(args, 1);
        std::ranges::transform(read, points.begin(),
                               [](double h) { return static_cast<double>(static_cast<std::uint32_t>(h)); });
        const double handle = nextHandle();
        return host->addPath(handle, binding::string(args, 0), points) ? std::vector<Value>{Value(handle)}
                                                                       : std::vector<Value>{Value()};
    };
}

// `IsInsideBox(box, object) -> boolean`: the object's position inside the (turned) box; false for a bad handle.
// @orig 0x00413198 VolumeBox_ContainsObject (unknown)
NativeFunction makeIsInsideBox(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const world_objects::VolumeBox* box =
            context->boxes != nullptr ? context->boxes->find(handleArg(args, 0)) : nullptr;
        const std::optional<StoryPoint> at = positionOf(*context, handleArg(args, 1));
        return binding::boolean(box != nullptr && at && world_objects::VolumeBoxes::inside(*box, *at));
    };
}

// `EnableVolumeBox(box, enable)`: a disabled box tests nobody, and its occupants are forgotten without a leave
// message, so they get a fresh enter once it is enabled again.
// @orig 0x00412bf8 VolumeBox_Enable (unknown)
// @orig 0x004152e0 VolumeBox_SetEnabled (unknown)
NativeFunction makeEnableVolumeBox(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        if (world_objects::VolumeBox* box =
                context->boxes != nullptr ? context->boxes->find(handleArg(args, 0)) : nullptr;
            box != nullptr) {
            box->enabled = boolArg(args, 1);
            if (!box->enabled) {
                box->occupants.clear();
            }
        }
        return binding::none();
    };
}

// `FlagGetOwner(flag) -> handle`: who uses the flag now; NilHandle for no one or no flag.
// @orig 0x00416ed0 Flag_GetOwner (unknown)
NativeFunction makeFlagGetOwner(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const double handle = handleArg(args, 0);
        const world_objects::WorldFlag* flag = context->flags != nullptr ? context->flags->find(handle) : nullptr;
        if (flag == nullptr) {
            return binding::number(kNilHandle);
        }
        const HumanBindingHost* host = humansOf(*context);
        const double user = flag->user != kNilHandle || host == nullptr ? flag->user : host->flagUser(handle);
        return binding::number(user);
    };
}

// `SetFlagPos(flag, {x, y, z})`: the flag's own position; one that follows a parent keeps reporting the parent's.
// **Coney choice**: a handle that names no flag does nothing (the original writes through it).
// @orig 0x00416b68 Flag_SetPosition (unknown)
NativeFunction makeSetFlagPos(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        world_objects::WorldFlag* flag = context->flags != nullptr ? context->flags->find(handleArg(args, 0)) : nullptr;
        if (flag != nullptr) {
            flag->position = binding::position(args, 1).value_or(std::array<float, 3>{});
        }
        return binding::none();
    };
}

// ---- The humans ----

// `HuGetControlName(human) -> string`: the control handler driving the human; nil for no human.
// @orig 0x002380c0 Human_GetControlName (unknown)
NativeFunction makeHuGetControlName(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const StoryBindingHost* host = storyOf(*context);
        const std::optional<std::string> name = host != nullptr ? host->controlName(handleArg(args, 0)) : std::nullopt;
        return name ? std::vector<Value>{Value(*name)} : std::vector<Value>{Value()};
    };
}

// `HuWhatAmIHolding(human) -> number`: the held object's kind (`TYPE_*`, its type's `+0x86`); 0 for nothing.
// @orig 0x00237d08 Human_GetHeldObjectType (unknown)
NativeFunction makeHuWhatAmIHolding(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const StoryBindingHost* host = storyOf(*context);
        const std::string held = host != nullptr ? host->heldObject(handleArg(args, 0)) : std::string{};
        const world_objects::ObjectType* type =
            held.empty() || context->objectTypes == nullptr ? nullptr : context->objectTypes->find(held);
        return binding::number(type != nullptr ? type->objectKind : 0);
    };
}

// A boolean getter of the human named by argument 0 (false without a host).
template <typename Read> NativeFunction storyQuery(const BindingContext& context, Read read) {
    return [context = &context, read](std::span<const Value> args) {
        const StoryBindingHost* host = storyOf(*context);
        return binding::boolean(host != nullptr && read(*host, args));
    };
}

// `HuSetInterrogation(human, line1, line2, line3, line4, callback, icon) -> boolean`: the lines cleared and set, the
// callback (nil: not interrogable) and the icon (any non-zero, default 1). True when the handle names a human.
// @orig 0x00239f20 Human_SetInterrogation (unknown)
NativeFunction makeHuSetInterrogation(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        StoryBindingHost* host = storyOf(*context);
        if (host == nullptr) {
            return binding::boolean(false);
        }
        const std::array<std::string, 4> lines{nameArg(args, 1), nameArg(args, 2), nameArg(args, 3), nameArg(args, 4)};
        const bool icon = intArgOr(args, 6, 1) != 0;
        return binding::boolean(host->setInterrogation(handleArg(args, 0), lines, nameArg(args, 5), icon));
    };
}

// `HuTag(human, tag, flag)`: the human sprays the tag spot `tag` from `flag`.
// @orig 0x00238db0 Human_Tag (unknown)
NativeFunction makeHuTag(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        host.tag(handleArg(args, 0), handleArg(args, 1), handleArg(args, 2));
    });
}

// `HuTagColor(human, {r, g, b, a})`: each element × 255 twice and its low byte kept (a whole 0-255 comes out as it
// is), packed with r in the top byte.
// @orig 0x00239080 Human_SetTagColour (unknown)
NativeFunction makeHuTagColor(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        const std::array<double, 4> colour = tableArg<4>(args, 1);
        std::uint32_t packed = 0;
        for (const double component : colour) {
            const double scaled = component * 255.0 * 255.0;
            const auto byte =
                scaled <= 0.0 ? 0U : static_cast<std::uint32_t>(static_cast<std::uint64_t>(scaled) & 0xffU);
            packed = (packed << 8U) | byte;
        }
        host.setTagColour(handleArg(args, 0), packed);
    });
}

// `HuTagPattern(count, {x1, y1, ...})`: the next tag's stick pattern; 64 points are kept.
// @orig 0x00239188 Tag_SetPattern (unknown)
NativeFunction makeHuTagPattern(const BindingContext& context) {
    return stateCall(context, [](StoryState& story, std::span<const Value> args) {
        story.tagPatternCount = static_cast<std::uint32_t>(wholeArg(args, 0));
        const std::array<double, kTagPatternRead> points = tableArg<kTagPatternRead>(args, 1);
        for (std::size_t i = 0; i < story.tagPattern.size(); ++i) {
            story.tagPattern.at(i) = static_cast<float>(points.at(i));
        }
    });
}

// `GoalMoveToExitFlag(human, flag, gait, angle, distance, radius)`.
// @orig 0x002da810 Goal_MoveToExitFlag (unknown)
NativeFunction makeGoalMoveToExitFlag(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        host.goalMoveToExitFlag(ExitFlagCall{.human = handleArg(args, 0),
                                             .flag = handleArg(args, 1),
                                             .gait = intArg(args, 2),
                                             .angle = floatArg(args, 3),
                                             .distance = floatArg(args, 4),
                                             .radius = floatArg(args, 5)});
    });
}

// `HuExitWorld(human)`: to the nearest exit flag its brain accepts, running, with a 2 m radius.
// @orig 0x00238478 Human_ExitWorld (unknown)
NativeFunction makeHuExitWorld(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        constexpr int kRun = 4;
        constexpr float kExitRadius = 2.0F;
        host.goalMoveToExitFlag(
            ExitFlagCall{.human = handleArg(args, 0), .flag = kNilHandle, .gait = kRun, .radius = kExitRadius});
    });
}

// `GoalTravelPath(human, path, mode, reverse, gait, radius)`: the path is read only when a second argument is given.
// @orig 0x002e05a8 Goal_TravelPath (unknown)
NativeFunction makeGoalTravelPath(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        host.goalTravelPath(TravelPathCall{.human = handleArg(args, 0),
                                           .path = absent(args, 1) ? kNilHandle : handleArg(args, 1),
                                           .mode = intArg(args, 2),
                                           .reverse = boolArg(args, 3),
                                           .gait = intArg(args, 4),
                                           .radius = floatArg(args, 5)});
    });
}

// `GoalThrowObject(human, target, range, gait, callback)`.
// @orig 0x002cf480 Goal_ThrowObject (unknown)
NativeFunction makeGoalThrowObject(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        constexpr float kThrowRange = 16.0F;
        host.goalThrowObject(ThrowObjectCall{.human = handleArg(args, 0),
                                             .target = handleArg(args, 1),
                                             .range = floatArgOr(args, 2, kThrowRange),
                                             .gait = intArgOr(args, 3, 2),
                                             .callback = nameArg(args, 4)});
    });
}

// `GoalGuardFlag(human, flag, radius, heading, callback, timeMs)`: the time defaults to -1.
// @orig 0x00360f28 GoalGuardFlag (unknown)
// @orig 0x002b79b8 Goal_GuardFlag (unknown)
NativeFunction makeGoalGuardFlag(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        host.goalGuardFlag(GuardFlagCall{.human = handleArg(args, 0),
                                         .flag = handleArg(args, 1),
                                         .radius = floatArg(args, 2),
                                         .heading = static_cast<int>(static_cast<std::int16_t>(intArg(args, 3))),
                                         .callback = nameArg(args, 4),
                                         .timeMs = intArgOr(args, 5, -1)});
    });
}

// `GoalLeadChase(human, path, chaser, distance1, distance2, distance3, distance4)`: the path is read only when a
// second argument is given; the chaser and the last three distances are not used by the goal.
// @orig 0x00360ac8 GoalLeadChase (unknown)
// @orig 0x002e0a78 Goal_LeadChase (unknown)
NativeFunction makeGoalLeadChase(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        host.goalLeadChase(LeadChaseCall{.human = handleArg(args, 0),
                                         .path = absent(args, 1) ? kNilHandle : handleArg(args, 1),
                                         .waitDistance = floatArg(args, 3)});
    });
}

// `GoalDevilRun(human, path, gang, gait, attackDistance, paceDistance, maxSpeed, urgency, hostile)`.
// @orig 0x00360d98 GoalDevilRun (unknown)
// @orig 0x002e1760 Goal_DevilRun (unknown)
NativeFunction makeGoalDevilRun(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        host.goalDevilRun(DevilRunCall{.human = handleArg(args, 0),
                                       .path = absent(args, 1) ? kNilHandle : handleArg(args, 1),
                                       .gang = static_cast<int>(static_cast<std::int16_t>(intArg(args, 2))),
                                       .gait = static_cast<int>(static_cast<std::int16_t>(intArg(args, 3))),
                                       .attackDistance = floatArg(args, 4),
                                       .paceDistance = floatArg(args, 5),
                                       .maxSpeed = floatArg(args, 6),
                                       .urgency = floatArg(args, 7),
                                       .hostile = boolArg(args, 8)});
    });
}

// `GoalBigLedgeThrower(human, targets, objects, cycles, delayMs, taunt, anim)`.
// @orig 0x00363b38 GoalBigLedgeThrower (unknown)
// @orig 0x002edf20 Goal_BigLedgeThrower (unknown)
NativeFunction makeGoalBigLedgeThrower(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        LedgeThrowerCall call;
        call.human = handleArg(args, 0);
        const std::array<double, 3> targets = tableArg<3>(args, 1);
        for (std::size_t i = 0; i < targets.size(); ++i) {
            call.targets.at(i) = static_cast<double>(static_cast<std::uint32_t>(targets.at(i)));
        }
        const std::array<double, 8> objects = tableArg<8>(args, 2);
        for (std::size_t i = 0; i < objects.size(); ++i) {
            call.objects.at(i) = static_cast<int>(static_cast<std::int16_t>(static_cast<std::int64_t>(objects.at(i))));
        }
        call.cycles = static_cast<int>(static_cast<std::uint8_t>(wholeArg(args, 3)));
        call.delayMs = static_cast<std::uint16_t>(wholeArg(args, 4));
        call.taunt = boolArg(args, 5);
        call.anim = nameArg(args, 6);
        host.goalBigLedgeThrower(call);
    });
}

// `GoalPlayDynIdle(human, flag, startAnim, loopAnim, endAnim, timeMs)`: nothing without a loop anim.
// @orig 0x002d3940 Goal_PlayDynamicIdle (unknown)
NativeFunction makeGoalPlayDynIdle(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        DynIdleCall call{.human = handleArg(args, 0),
                         .flag = optionalHandleArg(args, 1),
                         .startAnim = nameArg(args, 2),
                         .loopAnim = nameArg(args, 3),
                         .endAnim = nameArg(args, 4),
                         .timeMs = intArgOr(args, 5, -1)};
        if (!call.loopAnim.empty()) {
            host.goalPlayDynIdle(call);
        }
    });
}

// ---- The gangs ----

// `GangAddTurfBox(gang, box)`: only a volume box is taken.
// @orig 0x0016a328 Gang_AddTurfBox (unknown)
NativeFunction makeGangAddTurfBox(const BindingContext& context) {
    return storyCall(context, [context = &context](StoryBindingHost& host, std::span<const Value> args) {
        const double box = handleArg(args, 1);
        if (context->boxes != nullptr && context->boxes->find(box) != nullptr) {
            host.addTurfBox(intArg(args, 0), box);
        }
    });
}

// `GangIsWanted(gang, current) -> boolean`. **Coney stand-in**: Coney keeps one wanted state per gang (the crime
// report's 10 s), so both flags read it.
// @orig 0x0016b580 Gang_IsWanted (unknown)
NativeFunction makeGangIsWanted(const BindingContext& context) {
    return [context = &context](std::span<const Value> args) {
        const int gang = intArg(args, 0);
        return binding::boolean(gang >= 0 && context->state->player.crimes.wanted(gang));
    };
}

// `GangExitWorld(gang, exit, callback, deleteGang)`.
// @orig 0x0016a670 Gang_ExitWorld (unknown)
NativeFunction makeGangExitWorld(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        host.gangExitWorld(intArg(args, 0), optionalHandleArg(args, 1), nameArg(args, 2), boolArgOr(args, 3, true));
    });
}

// `GangStartSpawner(gang, name, mode, value)`: the value read as 16 bits; -1 keeps the spawner's.
// @orig 0x0016afc8 Gang_StartSpawner (unknown)
NativeFunction makeGangStartSpawner(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        host.startSpawner(intArg(args, 0), binding::string(args, 1), intArg(args, 2),
                          static_cast<std::int16_t>(intArgOr(args, 3, -1)));
    });
}

// `GangCanUseWorldFlags(gang, on, percent)`: the percentage forced to 0 when off.
// @orig 0x0016be30 Gang_CanUseWorldFlags (unknown)
NativeFunction makeGangCanUseWorldFlags(const BindingContext& context) {
    return storyCall(context, [](StoryBindingHost& host, std::span<const Value> args) {
        constexpr std::uint32_t kDefaultPercent = 10;
        const bool on = boolArgOr(args, 1, true);
        host.canUseWorldFlags(intArg(args, 0), on, on ? static_cast<int>(unsignedArgOr(args, 2, kDefaultPercent)) : 0);
    });
}

// ---- Configuration ----

// `CrimeIsHappening(pos, kind, radius, severity, offender, victim, flags)`: a crime report that sends responders; the
// radius, severity and flags are never read.
// @orig 0x0041b6e0 Crime_IsHappening (unknown)
NativeFunction makeCrimeIsHappening(ScriptSystem& scripts, const BindingContext& context) {
    return [scripts = &scripts, context = &context](std::span<const Value> args) {
        CrimeServices none;
        CrimeServices& services = context->crimes != nullptr ? *context->crimes : none;
        const std::array<float, 3> at = binding::position(args, 0).value_or(std::array<float, 3>{});
        context->state->player.crimes.report(services, static_cast<std::int16_t>(intArg(args, 1)), at,
                                             handleArg(args, 4), optionalHandleArg(args, 5), true, 0, scripts->now());
        return binding::none();
    };
}

// ---- The tactics ----

// The tactic `kind` for gang argument 0, read by `read` from the rest of the arguments.
template <typename Read> NativeFunction tacticCall(const BindingContext& context, TacticKind kind, Read read) {
    return storyCall(context, [kind, read](StoryBindingHost& host, std::span<const Value> args) {
        TacticCall call;
        call.gang = intArg(args, 0);
        call.kind = kind;
        read(call, args);
        host.setTactic(call);
    });
}

// The tactic bindings, each with its arguments and defaults (docs/references/bindings/ai.md); TacticAttack and
// TacticConfront are the Rumble's (rumble_match_bindings.h).
// @orig 0x00309a40 Tactic_BossDiegoVargas (unknown)
// @orig 0x00315d98 Tactic_Defend (unknown)
// @orig 0x00313e30 Tactic_HoldTheLine (unknown)
// @orig 0x00316a68 Tactic_ManWeaponPile (unknown)
// @orig 0x00315ee8 Tactic_Pursue (unknown)
// @orig 0x00315e48 Tactic_WalkinTall (unknown)
// @orig 0x003160a8 Tactic_Wander (unknown)
// @orig 0x003161a0 Tactic_TravelPath (unknown)
// @orig 0x00315fc8 Tactic_HanginOut (unknown)
// @orig 0x00316298 Tactic_MoveToFlag (unknown)
// @orig 0x003164e0 Tactic_Vandalize (unknown)
// @orig 0x00316638 Tactic_Steal (unknown)
// @orig 0x00316988 Tactic_AvoidEnemies (unknown)
// @orig 0x00316ca0 Tactic_UseFlag (unknown)
// @orig 0x00316ee8 Tactic_Idle (unknown)
// @orig 0x0031a268 Tactic_Scout (unknown)
void addTacticBindings(LuaVm& vm, const BindingContext& context) {
    using A = std::span<const Value>;
    vm.registerFunction("TacticBossScenarioA", tacticCall(context, TacticKind::BossDiegoVargas, [](TacticCall& c, A a) {
                            c.boss.stage = static_cast<int>(static_cast<std::uint32_t>(wholeArg(a, 1)));
                            const std::array<double, 2> flags = tableArg<2>(a, 2);
                            c.flags = {flags[0], flags[1], 0.0};
                            c.boss.vargasObjects = objectTypes(tableArg<8>(a, 3));
                            c.boss.minionObjects = objectTypes(tableArg<8>(a, 4));
                            // Each table pair is Diego's, then Vargas's.
                            for (std::size_t boss = 0; boss < 2; ++boss) {
                                c.boss.fatigue.at(boss) = tableInts(tableArg<3>(a, 5 + boss));
                                c.boss.damage.at(boss) = tableInts(tableArg<3>(a, 7 + boss));
                                c.boss.prone.at(boss) = tableInts(tableArg<3>(a, 9 + boss));
                                c.boss.cycles.at(boss) = tableInts(tableArg<3>(a, 11 + boss));
                            }
                            c.callback = nameArg(a, 13);
                        }));
    vm.registerFunction("TacticDefend", tacticCall(context, TacticKind::Defend, [](TacticCall& c, A a) {
                            c.flags.at(0) = handleArg(a, 1);
                            c.range = floatArgOr(a, 2, 2.25F);
                            c.callback = nameArg(a, 3);
                        }));
    vm.registerFunction("TacticHoldTheLine", tacticCall(context, TacticKind::HoldTheLine, [](TacticCall& c, A a) {
                            c.flags = {handleArg(a, 1), handleArg(a, 2), handleArg(a, 3)};
                            c.count = unsignedArgOr(a, 4, 5);
                            c.delayMs = unsignedArgOr(a, 5, 20) * 1000U;
                            c.range2 = floatArgOr(a, 6, 10.0F);
                            c.callback = nameArg(a, 7);
                        }));
    vm.registerFunction("TacticManWeaponPile", tacticCall(context, TacticKind::ManWeaponPile, [](TacticCall& c, A a) {
                            c.range = floatArg(a, 1);
                            c.range2 = floatArg(a, 2);
                            c.count = static_cast<std::uint16_t>(intArg(a, 3));
                            c.count2 = static_cast<std::uint16_t>(intArg(a, 4));
                            c.callback = nameArg(a, 5);
                        }));
    vm.registerFunction("TacticPursue", tacticCall(context, TacticKind::Pursue, [](TacticCall& c, A a) {
                            c.targetGang = intArgOr(a, 1, -1);
                            c.range = floatArgOr(a, 2, 5.0F);
                            c.gait = intArgOr(a, 3, 2);
                            c.range2 = floatArgOr(a, 4, 90.0F);
                            c.range3 = floatArgOr(a, 5, 45.0F);
                            c.callback = nameArg(a, 6);
                            c.delayMs = unsignedArgOr(a, 7, 30000);
                        }));
    vm.registerFunction("TacticWalkinTall", tacticCall(context, TacticKind::WalkinTall, [](TacticCall& c, A a) {
                            c.flags.at(0) = handleArg(a, 1);
                            c.range = floatArg(a, 2);
                            c.callback = nameArg(a, 3);
                        }));
    vm.registerFunction("TacticWander", tacticCall(context, TacticKind::Wander, [](TacticCall& c, A a) {
                            c.gait = intArgOr(a, 1, 2);
                            c.delayMs = unsignedArgOr(a, 2, 0);
                            c.callback = nameArg(a, 3);
                            c.range2 = floatArgOr(a, 4, 10.0F);
                            c.slotSet = intArgOr(a, 5, -1);
                            c.options.at(kTacticBanter) = boolArgOr(a, 6, true);
                            c.options.at(kTacticLoop) = boolArgOr(a, 7, true);
                            c.options.at(kTacticReverse) = boolArg(a, 8) || boolArg(a, 9);
                        }));
    vm.registerFunction("TacticTravelPath", tacticCall(context, TacticKind::TravelPath, [](TacticCall& c, A a) {
                            c.flags.at(0) = absent(a, 1) ? kNilHandle : handleArg(a, 1);
                            c.options.at(kTacticReverse) = boolArg(a, 2);
                            c.gait = intArgOr(a, 3, 2);
                            c.delayMs = unsignedArgOr(a, 4, 0) * 1000U;
                            c.slotSet = intArgOr(a, 5, -1);
                            c.startPoint = intArgOr(a, 6, -1);
                            c.callback = nameArg(a, 7);
                            c.options.at(kTacticBanter) = boolArgOr(a, 8, true);
                            c.options.at(kTacticLoop) = boolArgOr(a, 9, true);
                        }));
    vm.registerFunction("TacticHanginOut", tacticCall(context, TacticKind::HanginOut, [](TacticCall& c, A a) {
                            c.flags.at(0) = handleArg(a, 1);
                            c.callback = nameArg(a, 2);
                            c.range = floatArgOr(a, 3, 6.0F);
                            c.options.at(kTacticBanter) = boolArgOr(a, 4, true);
                            c.options.at(kTacticRespond) = boolArg(a, 5);
                            c.options.at(kTacticAware) = boolArg(a, 6);
                            c.options.at(kTacticHarass) = boolArgOr(a, 7, true);
                        }));
    vm.registerFunction("TacticMoveToFlag", tacticCall(context, TacticKind::MoveToFlag, [](TacticCall& c, A a) {
                            c.flags.at(0) = handleArg(a, 1);
                            c.gait = intArgOr(a, 2, 2);
                            c.slotSet = intArgOr(a, 3, -1);
                            c.callback = nameArg(a, 4);
                            c.options.at(kTacticBanter) = boolArg(a, 5);
                        }));
    vm.registerFunction("TacticVandalize", tacticCall(context, TacticKind::Vandalize, [](TacticCall& c, A a) {
                            c.zone = static_cast<std::uint32_t>(wholeArg(a, 1));
                            c.delayMs = unsignedArgOr(a, 2, 3000);
                            c.callback = nameArg(a, 3);
                            c.range = floatArgOr(a, 4, -1.0F);
                        }));
    vm.registerFunction("TacticSteal", tacticCall(context, TacticKind::Steal, [](TacticCall& c, A a) {
                            c.zone = static_cast<std::uint32_t>(wholeArg(a, 1));
                            c.delayMs = unsignedArgOr(a, 2, 1000);
                            c.callback = nameArg(a, 3);
                            c.range = floatArgOr(a, 4, -1.0F);
                        }));
    vm.registerFunction("TacticAvoidEnemies", tacticCall(context, TacticKind::AvoidEnemies, [](TacticCall& c, A a) {
                            c.callback = nameArg(a, 1);
                            c.options.at(kTacticHarass) = boolArg(a, 2);
                            c.range = floatArgOr(a, 3, 7.0F);
                            c.range2 = floatArgOr(a, 4, 14.0F);
                            c.range3 = floatArg(a, 5);
                            c.range4 = floatArg(a, 6);
                            c.gait = static_cast<int>(unsignedArgOr(a, 7, 0));
                        }));
    vm.registerFunction("TacticUseFlag", tacticCall(context, TacticKind::UseFlag, [](TacticCall& c, A a) {
                            c.flags.at(0) = handleArg(a, 1);
                            c.range = floatArgOr(a, 2, 4.0F);
                            c.range2 = floatArgOr(a, 3, 10.0F);
                            c.callback = nameArg(a, 4);
                            c.options.at(kTacticBanter) = boolArg(a, 5);
                        }));
    vm.registerFunction("TacticIdle", tacticCall(context, TacticKind::Idle, [](TacticCall& c, A a) {
                            c.options.at(kTacticBanter) = boolArgOr(a, 1, true);
                            c.options.at(kTacticRespond) = boolArg(a, 2);
                            c.options.at(kTacticAware) = boolArg(a, 3);
                            c.callback = nameArg(a, 4);
                            c.options.at(kTacticLoop) = boolArg(a, 5);
                        }));
    vm.registerFunction("TacticScout", tacticCall(context, TacticKind::Scout, [](TacticCall& c, A a) {
                            c.count = static_cast<std::uint32_t>(intArg(a, 1));
                            c.count2 = static_cast<std::uint32_t>(intArg(a, 2));
                            c.range = floatArgOr(a, 3, 40.0F);
                            c.range2 = floatArgOr(a, 4, 10.0F);
                            c.range3 = floatArgOr(a, 5, 30.0F);
                            c.callback = nameArg(a, 6);
                        }));
}

} // namespace

void giveWarriorCommand(ScriptSystem& scripts, const BindingContext& context, double chief, int command) {
    dispatchWarriorCommand(scripts, context, chief, command, false);
}

void addStoryBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context,
                      std::function<double()> nextHandle) {
    // The Warrior commands.
    vm.registerFunction("IssueWarriorCommand", makeIssueWarriorCommand(scripts, context));
    vm.registerFunction("WCIssueCommand", [scripts = &scripts, context = &context](std::span<const Value> args) {
        // `WCIssueCommand(player, command, on)`: `on` is the dispatcher's forced flag (the scripts pass true).
        // @orig 0x0041dc80 GameState_IssueWarriorCommandFor (unknown)
        dispatchWarriorCommand(*scripts, *context, handleArg(args, 0), intArg(args, 1), boolArg(args, 2));
        return binding::none();
    });
    vm.registerFunction("WCEnableCommand", makeWcEnableCommand(context));
    vm.registerFunction("WCLockCommands", makeWcLockCommands(context));
    vm.registerFunction("WCSetCallback", makeWcSetCallback(context));
    vm.registerFunction("HUDShowWarCommand", makeHudShowWarCommand(context));

    // Distances, paths, boxes and flags.
    vm.registerFunction("GetDistanceTweenHumans", makeGetDistanceTweenHumans(context));
    vm.registerFunction("TestDistance", makeTestDistance(context));
    vm.registerFunction("WalkingDistance", makeWalkingDistance(context));
    vm.registerFunction("PathValid", makePathValid(context));
    vm.registerFunction("AddPath", makeAddPath(context, std::move(nextHandle)));
    vm.registerFunction("IsInsideBox", makeIsInsideBox(context));
    vm.registerFunction("EnableVolumeBox", makeEnableVolumeBox(context));
    vm.registerFunction("FlagGetOwner", makeFlagGetOwner(context));
    vm.registerFunction("SetFlagPos", makeSetFlagPos(context));

    // The humans' switches: bits of the flag word (only exactly true sets the keep-hat and revivable bits).
    // @orig 0x0023a2c0 Human_SetBlockLook (unknown)
    vm.registerFunction("HuBlockLook", flagCall(context, human::flag::kBlockLook, boolArg));
    // @orig 0x0023a328 Human_SetForceLook (unknown)
    vm.registerFunction("HuForceLook", flagCall(context, human::flag::kForceLook, boolArg));
    // @orig 0x00235200 Human_SetAutoEscape (unknown)
    vm.registerFunction("HuSetAutoEscape", flagCall(context, human::flag::kAutoEscape, boolArg));
    // @orig 0x00237388 Human_SetKeepHat (unknown)
    vm.registerFunction("HuSetKeepHat", flagCall(context, human::flag::kKeepHat, exactlyOne));
    // @orig 0x00235db0 Human_SetRevivable (unknown)
    vm.registerFunction("HuSetRevivable", flagCall(context, human::flag::kRevivable, exactlyOne));
    // @orig 0x00237958 Human_SetBlockJump (unknown)
    vm.registerFunction("HuBlockJump", flagCall(context, human::flag::kBlockJump, boolArg));
    // @orig 0x00234118 Human_SetAutoCombat (unknown)
    vm.registerFunction("HuSetAutoCombat", flagCall(context, human::flag::kAutoCombat, boolArg));
    // @orig 0x00235198 Human_SetNoReact (unknown)
    vm.registerFunction("HuSetNoReact", flagCall(context, human::flag::kNoReact, boolArg));
    // `HuClearLook(human)`: the original's function returns at once.
    // @orig 0x0023a460 Human_ClearLook_Stub (unknown)
    vm.registerFunction("HuClearLook", [](std::span<const Value>) { return binding::none(); });
    vm.registerFunction("HuSetInterrogation", makeHuSetInterrogation(context));
    // @orig 0x00236038 Human_ApplyDamageModifier (unknown)
    vm.registerFunction("HuApplyDamageModifier", storyCall(context, [](StoryBindingHost& h, std::span<const Value> a) {
                            h.applyDamageModifier(handleArg(a, 0), floatArg(a, 1));
                        }));
    // @orig 0x0016ae60 Gang_MakeEnemiesOfType (unknown)
    vm.registerFunction("GangMakeEnemiesOfType", storyCall(context, [](StoryBindingHost& h, std::span<const Value> a) {
                            h.makeEnemiesOfType(intArg(a, 0), intArg(a, 1));
                        }));
    // @orig 0x0016bde8 Gang_SetAlwaysSeen (unknown)
    vm.registerFunction("GangSetAlwaysSeen", storyCall(context, [](StoryBindingHost& h, std::span<const Value> a) {
                            h.setAlwaysSeen(intArg(a, 0), boolArg(a, 1));
                        }));

    // The humans.
    using A = std::span<const Value>;
    // @orig 0x00237e70 Human_Kill (unknown)
    vm.registerFunction("HuKill", storyCall(context, [](StoryBindingHost& h, A a) { h.killHuman(handleArg(a, 0)); }));
    // `HuSetHealth(human, health)`: 16 bits.
    // @orig 0x00237848 Human_SetHealth (unknown)
    vm.registerFunction("HuSetHealth", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.setHealth(handleArg(a, 0), static_cast<std::int16_t>(intArg(a, 1)));
                        }));
    // @orig 0x00238030 Human_SetShadow (unknown)
    vm.registerFunction(
        "HuShadow", storyCall(context, [](StoryBindingHost& h, A a) { h.setShadow(handleArg(a, 0), boolArg(a, 1)); }));
    // @orig 0x0023ae90 Human_LockPadMovement (unknown)
    vm.registerFunction("HuLockPadMovement", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.lockMovement(handleArg(a, 0), boolArg(a, 1));
                        }));
    // @orig 0x002383a0 Human_SetLOSRange (unknown)
    vm.registerFunction("HuSetLOSRange", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.setSightRange(handleArg(a, 0), floatArg(a, 1));
                        }));
    // @orig 0x002928d8 Brain_SetFieldOfView (unknown)
    vm.registerFunction("BrSetFOV", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.setFieldOfView(handleArg(a, 0), floatArg(a, 1));
                        }));
    // @orig 0x002927a8 Brain_SetInvestigateResponse (unknown)
    vm.registerFunction("BrSetInvestigateResponse", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.setInvestigateResponse(handleArg(a, 0), intArg(a, 1));
                        }));
    // @orig 0x00292bc8 Brain_SetReactsToViolence (unknown)
    vm.registerFunction("BrSetReactToViolence", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.setReactToViolence(handleArg(a, 0), boolArgOr(a, 1, true));
                        }));
    vm.registerFunction("HuGetControlName", makeHuGetControlName(context));
    vm.registerFunction("HuWhatAmIHolding", makeHuWhatAmIHolding(context));
    // @orig 0x0023a468 Human_IsAimingAt (unknown)
    vm.registerFunction("HuIsAimingAt", storyQuery(context, [](const StoryBindingHost& h, A a) {
                            return h.aimingAt(handleArg(a, 0), handleArg(a, 1));
                        }));
    // @orig 0x002355e0 Human_IsGrabbed (unknown)
    vm.registerFunction("HuIsGrabbed",
                        storyQuery(context, [](const StoryBindingHost& h, A a) { return h.grabbed(handleArg(a, 0)); }));
    // @orig 0x002387a8 Human_AreActionsBlocked (unknown)
    vm.registerFunction("HuAreActionsBlocked", storyQuery(context, [](const StoryBindingHost& h, A a) {
                            return h.actionsBlocked(handleArg(a, 0));
                        }));
    vm.registerFunction("HuTag", makeHuTag(context));
    vm.registerFunction("HuTagColor", makeHuTagColor(context));
    vm.registerFunction("HuTagPattern", makeHuTagPattern(context));

    // The goals.
    vm.registerFunction("GoalMoveToExitFlag", makeGoalMoveToExitFlag(context));
    vm.registerFunction("HuExitWorld", makeHuExitWorld(context));
    vm.registerFunction("GoalTravelPath", makeGoalTravelPath(context));
    vm.registerFunction("GoalThrowObject", makeGoalThrowObject(context));
    vm.registerFunction("GoalGuardFlag", makeGoalGuardFlag(context));
    vm.registerFunction("GoalLeadChase", makeGoalLeadChase(context));
    vm.registerFunction("GoalDevilRun", makeGoalDevilRun(context));
    vm.registerFunction("GoalBigLedgeThrower", makeGoalBigLedgeThrower(context));
    vm.registerFunction("GoalPlayDynIdle", makeGoalPlayDynIdle(context));
    // `GoalMelee(human, target)`: NilHandle (the default) lets it pick.
    // @orig 0x002add08 Goal_Melee (unknown)
    vm.registerFunction("GoalMelee", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.goalMelee(handleArg(a, 0), optionalHandleArg(a, 1));
                        }));
    // @orig 0x002abe08 Goal_BumTrigger (unknown)
    vm.registerFunction("GoalBumLogicTrigger",
                        storyCall(context, [](StoryBindingHost& h, A a) { h.bumTrigger(handleArg(a, 0)); }));

    // The gangs.
    vm.registerFunction("GangAddTurfBox", makeGangAddTurfBox(context));
    // @orig 0x0016a3a8 Gang_RemoveTurfBox (unknown)
    vm.registerFunction("GangRemoveTurfBox", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.removeTurfBox(intArg(a, 0), handleArg(a, 1));
                        }));
    // @orig 0x0016a870 Gang_EngageEnemy (unknown)
    vm.registerFunction("GangEngageEnemy", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.engageEnemy(intArg(a, 0), handleArg(a, 1));
                        }));
    vm.registerFunction("GangIsWanted", makeGangIsWanted(context));
    // @orig 0x0016b4f0 Gang_SetInvestigateResponse (unknown)
    vm.registerFunction("GangSetInvestigateResponse", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.setGangInvestigateResponse(intArg(a, 0), intArg(a, 1));
                        }));
    // `GangSetRespondPercentage(gang, percent)`: one byte.
    // @orig 0x0016a2e8 Gang_SetRespondPercentage (unknown)
    vm.registerFunction("GangSetRespondPercentage", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.setRespondPercentage(
                                static_cast<std::int16_t>(intArg(a, 0)),
                                static_cast<int>(static_cast<std::uint32_t>(wholeArg(a, 1)) & 0xffU));
                        }));
    // @orig 0x0016bbf0 Gang_SetHearRange (unknown)
    vm.registerFunction("GangSetHearRange", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.setHearRange(static_cast<std::int16_t>(intArg(a, 0)), boolArg(a, 1), floatArg(a, 2));
                        }));
    // @orig 0x0016a2a8 Gang_EnableAttackStrategies (unknown)
    vm.registerFunction("GangEnableAttackStrategies", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.enableAttackStrategies(intArg(a, 0), boolArgOr(a, 1, true));
                        }));
    // @orig 0x0016a538 Gang_SetLeader (unknown)
    vm.registerFunction("GangSetLeader", storyCall(context, [](StoryBindingHost& h, A a) {
                            h.setLeader(intArg(a, 0), handleArg(a, 1));
                        }));
    // `GangGetLeader(gang) -> handle`: NilHandle for none.
    // @orig 0x0016a578 Gang_GetLeaderHandle (unknown)
    vm.registerFunction("GangGetLeader", [context = &context](A a) {
        const StoryBindingHost* host = storyOf(*context);
        return binding::number(host != nullptr ? host->leader(intArg(a, 0)) : kNilHandle);
    });
    vm.registerFunction("GangExitWorld", makeGangExitWorld(context));
    vm.registerFunction("GangStartSpawner", makeGangStartSpawner(context));
    vm.registerFunction("GangCanUseWorldFlags", makeGangCanUseWorldFlags(context));

    // The configuration the missions set.
    // @orig 0x0041d930 Cfg_SetDisableMusicForScenes (unknown)
    vm.registerFunction("CfgDisableMusicForScenes",
                        stateCall(context, [](StoryState& s, A a) { s.noMusicInScenes = boolArg(a, 0); }));
    // @orig 0x0041d728 Cfg_SetOutdoorMode (unknown)
    vm.registerFunction("CfgSetOutdoorMode", stateCall(context, [](StoryState& s, A a) { s.outdoor = boolArg(a, 0); }));
    // @orig 0x0041d920 Cfg_SetGangSizeForCombatMusic (unknown)
    vm.registerFunction("CfgGangSizeForCombatMusic",
                        stateCall(context, [](StoryState& s, A a) { s.combatMusicGangSize = intArg(a, 0); }));
    // @orig 0x00299510 GameState_SetSpawnMax (unknown)
    vm.registerFunction("SetSpawnMax", stateCall(context, [](StoryState& s, A a) {
                            s.spawnMax = static_cast<std::int16_t>(intArg(a, 0));
                        }));
    // @orig 0x0041da60 Cfg_SetGrappleCounters (unknown)
    vm.registerFunction("CfgEnableGrappleCounters",
                        stateCall(context, [](StoryState& s, A a) { s.grappleCounters = boolArgOr(a, 0, true); }));
    // `CfgCivilianAggression(a, b)`: two bytes.
    // @orig 0x00294828 Cfg_SetCivilianAggression (unknown)
    vm.registerFunction("CfgCivilianAggression", stateCall(context, [](StoryState& s, A a) {
                            s.civilianAggression = {
                                static_cast<int>(static_cast<std::uint32_t>(wholeArg(a, 0)) & 0xffU),
                                static_cast<int>(static_cast<std::uint32_t>(wholeArg(a, 1)) & 0xffU)};
                        }));
    // @orig 0x00294808 Cfg_SetVerticalSightModifier (unknown)
    vm.registerFunction("CfgVerticalSightModifier",
                        stateCall(context, [](StoryState& s, A a) { s.verticalSight = floatArg(a, 0); }));
    // @orig 0x00238f10 Cfg_SetTagStartCallback (unknown)
    vm.registerFunction("CfgTagStartCallback",
                        stateCall(context, [](StoryState& s, A a) { s.tagStartCallback = nameArg(a, 0); }));
    // `CfgCrimeResponders(crime, count)`: a byte per crime type.
    // @orig 0x0041d8a0 Cfg_SetCrimeResponders (unknown)
    vm.registerFunction("CfgCrimeResponders", [context = &context](A a) {
        context->state->player.crimes.setResponders(
            static_cast<int>(static_cast<std::uint32_t>(wholeArg(a, 0)) & 0xffU),
            static_cast<int>(static_cast<std::uint32_t>(wholeArg(a, 1)) & 0xffU));
        return binding::none();
    });
    vm.registerFunction("CrimeIsHappening", makeCrimeIsHappening(scripts, context));
    // `SetCharacterModel(type, release)`: the type kept (or dropped) in the level's model list.
    // @orig 0x0040cda8 ResourceManager_SetCharacterModel (unknown)
    vm.registerFunction("SetCharacterModel", stateCall(context, [](StoryState& s, A a) {
                            const int type = intArg(a, 0);
                            std::erase(s.keptModels, type);
                            if (!boolArg(a, 1) && s.keptModels.size() < kKeptModels) {
                                s.keptModels.push_back(type);
                            }
                        }));
    // `setDetailFlag(index, mask)` and `clearDetailFlag(index, mask)`: one of four bytes nothing reads. **Coney
    // choice**: an index outside 0-3 does nothing (the original writes past them).
    // @orig 0x0041d830 GameState_SetDetailFlag (unknown)
    vm.registerFunction("setDetailFlag", stateCall(context, [](StoryState& s, A a) {
                            if (const std::int64_t i = wholeArg(a, 0);
                                i >= 0 && i < static_cast<std::int64_t>(kDetailBytes)) {
                                s.detailFlags.at(static_cast<std::size_t>(i)) |=
                                    static_cast<std::uint8_t>(static_cast<std::uint32_t>(wholeArg(a, 1)) & 0xffU);
                            }
                        }));
    // @orig 0x0041d860 GameState_ClearDetailFlag (unknown)
    vm.registerFunction("clearDetailFlag", stateCall(context, [](StoryState& s, A a) {
                            if (const std::int64_t i = wholeArg(a, 0);
                                i >= 0 && i < static_cast<std::int64_t>(kDetailBytes)) {
                                s.detailFlags.at(static_cast<std::size_t>(i)) &=
                                    static_cast<std::uint8_t>(~static_cast<std::uint32_t>(wholeArg(a, 1)) & 0xffU);
                            }
                        }));

    addTacticBindings(vm, context);
}

} // namespace coney::script
