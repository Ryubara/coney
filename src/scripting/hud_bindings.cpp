// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/hud_bindings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "hud/hud.h"
#include "scripting/binding_args.h"
#include "warriors/game_state.h"

namespace coney::script {

namespace {

// `W_ShowStopWatch`'s warning window when none is given, ms.
constexpr int kStopWatchWarnMs = 10000;

// Argument `i` truncated to a whole number, as tolua reads an integer.
std::int64_t wholeArg(std::span<const Value> args, std::size_t i) {
    return static_cast<std::int64_t>(std::trunc(binding::number(args, i)));
}

// Whether argument `i` is missing or nil, so a default applies.
bool absent(std::span<const Value> args, std::size_t i) { return i >= args.size() || args[i].isNil(); }

// Argument `i` as an integer, `fallback` when omitted.
int intArg(std::span<const Value> args, std::size_t i, int fallback = 0) {
    return absent(args, i) ? fallback : static_cast<int>(wholeArg(args, i));
}

// Argument `i` as tolua reads an unsigned integer (kept to 32 bits), `fallback` when omitted.
std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i, std::uint32_t fallback = 0) {
    return absent(args, i) ? fallback : static_cast<std::uint32_t>(wholeArg(args, i));
}

// Argument `i` as a float, `fallback` when omitted.
float floatArg(std::span<const Value> args, std::size_t i, float fallback = 0.0F) {
    return absent(args, i) ? fallback : static_cast<float>(binding::number(args, i));
}

// Argument `i` as a boolean: nil and 0 are false.
bool boolArg(std::span<const Value> args, std::size_t i) {
    if (absent(args, i)) {
        return false;
    }
    return binding::number(args, i) != 0.0 || args[i].type() != Value::Type::Number;
}

// A handle argument: truncated to an unsigned integer.
double handleArg(std::span<const Value> args, std::size_t i) { return static_cast<double>(unsignedArg(args, i)); }

// A player argument (0 or 1) as an index, or nothing for any other value.
std::optional<std::size_t> playerArg(std::span<const Value> args, std::size_t i) {
    const std::uint32_t player = unsignedArg(args, i);
    return player < hud::kPlayers ? std::optional<std::size_t>(player) : std::nullopt;
}

// A binding that hands its arguments to the HUD when there is one and returns nothing.
template <typename Body> NativeFunction hudCall(const BindingContext& context, Body body) {
    return [hud = context.hud, body](std::span<const Value> args) {
        if (hud != nullptr) {
            body(*hud, args);
        }
        return binding::none();
    };
}

// The radar blip of `object`, made with `type` when it has none yet.
hud::RadarBlip& blipOf(hud::Hud& hud, double object, int type) {
    auto [found, made] = hud.radar().blips.try_emplace(object);
    if (made) {
        found->second.type = type;
    }
    return found->second;
}

// The string bindings' HUD strings and the CfgHUDColor recordings as the HUD's services.
std::string recordedHudColour(const RecordedCalls* recorded, int slot) {
    if (recorded == nullptr) {
        return {};
    }
    // The last call for the slot wins, as the original's table keeps the last pointer.
    std::string colour;
    for (const std::vector<Value>& call : recorded->calls("CfgHUDColor")) {
        if (call.size() >= 2 && static_cast<int>(binding::number(call, 0)) == slot) {
            colour = binding::string(call, 1);
        }
    }
    return colour;
}

// The sound `SoundCfgInterfaceSound(cue, name)` last set for `cue`, or none (docs/research/sound.md#interface-sounds).
std::string recordedInterfaceSound(const RecordedCalls* recorded, int cue) {
    if (recorded == nullptr) {
        return {};
    }
    std::string name;
    for (const std::vector<Value>& call : recorded->calls("SoundCfgInterfaceSound")) {
        if (call.size() >= 2 && static_cast<int>(binding::number(call, 0)) == cue) {
            name = binding::string(call, 1);
        }
    }
    return name;
}

// The bindings, in kHudBindings order where it reads naturally.
void addPanelBindings(LuaVm& vm, const BindingContext& context) {
    // `FlashRageBar(player, n)`.
    // @orig 0x00370d20 FlashRageBar (unknown)
    vm.registerFunction("FlashRageBar", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            if (const auto player = playerArg(args, 0)) {
                                hud.panel(*player).setFlashFrames(unsignedArg(args, 1));
                            }
                        }));
    // `ForceShowPlayerHud(player, on)`.
    // @orig 0x0036ebe0 ForceShowPlayerHud (unknown)
    vm.registerFunction("ForceShowPlayerHud", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            if (const auto player = playerArg(args, 0)) {
                                hud.panel(*player).setForceShow(boolArg(args, 1));
                            }
                        }));
    // @orig 0x00370ca0 HideHud (unknown)
    vm.registerFunction("HideHud", hudCall(context, [](hud::Hud& hud, std::span<const Value>) { hud.hideAll(); }));
    // @orig 0x00370cc0 RestoreHud (unknown)
    vm.registerFunction("RestoreHud", hudCall(context, [](hud::Hud& hud, std::span<const Value>) { hud.showAll(); }));
    // `ShowHud(n)` does nothing in this build (0x001b3ec8 returns at once).
    // @orig 0x00370c68 ShowHud (unknown)
    vm.registerFunction("ShowHud", [](std::span<const Value>) { return binding::none(); });
    // @orig 0x00370ce0 HidePlayerHud (unknown)
    vm.registerFunction("HidePlayerHud",
                        hudCall(context, [](hud::Hud& hud, std::span<const Value>) { hud.hidePlayers(); }));
    // @orig 0x00370d00 ShowPlayerHud (unknown)
    vm.registerFunction("ShowPlayerHud",
                        hudCall(context, [](hud::Hud& hud, std::span<const Value>) { hud.showPlayers(); }));
}

// The objectives, the hints and the announcement.
void addMessageBindings(LuaVm& vm, const BindingContext& context) {
    // `HUDSetObjective(slot, text, mode, silent, ms)`.
    // @orig 0x0036f280 HUDSetObjective (unknown)
    vm.registerFunction("HUDSetObjective", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.setObjective(intArg(args, 0), binding::string(args, 1), intArg(args, 2),
                                             boolArg(args, 3), unsignedArg(args, 4, hud::kObjectiveMessageMs));
                        }));
    // @orig 0x0036f360 HUDRemoveAllGoalText (unknown)
    vm.registerFunction("HUDRemoveAllGoalText",
                        hudCall(context, [](hud::Hud& hud, std::span<const Value>) { hud.removeGoalText(); }));
    // `HUDSetTutorialText(text, priority)`: nil clears the showing hint (the game is never paused under a script in
    // Coney).
    // @orig 0x0036f380 HUDSetTutorialText (unknown)
    vm.registerFunction("HUDSetTutorialText", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            if (absent(args, 0)) {
                                hud.hints().clearShowing();
                                return;
                            }
                            hud.hints().queue(binding::string(args, 0), static_cast<int>(unsignedArg(args, 1, 1)));
                        }));
    // @orig 0x0036f430 HUDFlushTutorialText (unknown)
    vm.registerFunction("HUDFlushTutorialText", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.hints().flush(static_cast<int>(unsignedArg(args, 0, hud::kHintFlushAll)));
                        }));
    // `HUDCheckTutorialText(text)`: 1 when showing or queued, else nil.
    // @orig 0x0036f3e8 HUDCheckTutorialText (unknown)
    vm.registerFunction("HUDCheckTutorialText", [hud = context.hud](std::span<const Value> args) {
        return binding::boolean(hud != nullptr && hud->hints().contains(binding::string(args, 0)));
    });
    // @orig 0x00370098 HUDSetTutorialCallback (unknown)
    vm.registerFunction("HUDSetTutorialCallback", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.setTutorialCallback(absent(args, 0) ? std::string{} : binding::string(args, 0));
                        }));
    // @orig 0x001b5ff0 GameState_SetTutorialText (unknown)
    // @orig 0x0036eec8 HUDEnableGameTutorialText (unknown)
    vm.registerFunction("HUDEnableGameTutorialText", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.setGameTutorialText(boolArg(args, 0));
                        }));
    // @orig 0x0036e858 HUDSetAnnounceMsg (unknown)
    vm.registerFunction("HUDSetAnnounceMsg", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.setAnnouncement(static_cast<int>(unsignedArg(args, 0)), binding::string(args, 1),
                                                boolArg(args, 2));
                        }));
    // `HUDShowMissionSummaryText(text, n)` does nothing in this build (0x001b3878 returns at once).
    // @orig 0x0036f0d0 HUDShowMissionSummaryText (unknown)
    vm.registerFunction("HUDShowMissionSummaryText", [](std::span<const Value>) { return binding::none(); });
}

// The counter panels and the instruction arrow.
void addPanelAndArrowBindings(LuaVm& vm, const BindingContext& context) {
    // `HUDGetNewPH(kind, label, width)`: the panel, or -1 as unsigned.
    // @orig 0x0036e998 HUDGetNewPH (unknown)
    vm.registerFunction("HUDGetNewPH", [hud = context.hud](std::span<const Value> args) {
        if (hud == nullptr) {
            return binding::number(kNoCounterPanel);
        }
        const int panel = hud->counterPanels().take(static_cast<int>(unsignedArg(args, 0)), binding::string(args, 1),
                                                    floatArg(args, 2, hud::kCounterPanelBarWidth));
        return binding::number(panel < 0 ? kNoCounterPanel : static_cast<double>(panel));
    });
    // @orig 0x0036ea40 HUDReleasePH (unknown)
    vm.registerFunction("HUDReleasePH", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.counterPanels().release(intArg(args, 0));
                        }));
    // `HUDSetPHValue(panel, field, value, show, ms, sound)`: `show` is passed on in the original with no traced
    // effect, so it is read and not used.
    // @orig 0x0036ea78 HUDSetPHValue (unknown)
    vm.registerFunction("HUDSetPHValue", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.counterPanels().setValue(intArg(args, 0), intArg(args, 1), floatArg(args, 2),
                                                         unsignedArg(args, 4, hud::kCounterPanelDefaultMs),
                                                         boolArg(args, 5), hud.nowMs(), hud.services().sound);
                        }));
    // @orig 0x0036fdd0 HUDEnableInstArrow (unknown)
    vm.registerFunction("HUDEnableInstArrow", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.enableArrow(boolArg(args, 0), floatArg(args, 1), floatArg(args, 2), floatArg(args, 3));
                        }));
    // @orig 0x0036fe90 HUDSetInstArrowAnimSpeed (unknown)
    vm.registerFunction("HUDSetInstArrowAnimSpeed", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.arrow().speed = unsignedArg(args, 0);
                        }));
}

// The radars.
void addRadarBindings(LuaVm& vm, const BindingContext& context) {
    // @orig 0x003700c8 HUDTurnOnRadar (unknown)
    vm.registerFunction("HUDTurnOnRadar", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.radarOn(intArg(args, 0, 2));
                        }));
    // @orig 0x00370100 HUDTurnOffRadar (unknown)
    vm.registerFunction("HUDTurnOffRadar", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.radarOff(intArg(args, 0, 2));
                        }));
    // `HUDSetNumIndicator(player, on, gang)`: gang defaults to -1 (none).
    // @orig 0x00370138 HUDSetNumIndicator (unknown)
    vm.registerFunction("HUDSetNumIndicator", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.setNumIndicator(intArg(args, 0), boolArg(args, 1), intArg(args, 2, -1));
                        }));
    // Blip types: 10 a mission objective, 1 a secondary one, and a human's by its class (Coney: 7, the class is not
    // read yet).
    // @orig 0x001b3f98 HUD_RadarAddObjective (unknown)
    // @orig 0x00370760 HUDAddRadarMissionObjective (unknown)
    vm.registerFunction("HUDAddRadarMissionObjective", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.radar().blips[handleArg(args, 0)] = hud::RadarBlip{.type = 10};
                        }));
    // @orig 0x00370798 HUDAddSecondaryRadarMissionObjective (unknown)
    vm.registerFunction("HUDAddSecondaryRadarMissionObjective",
                        hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.radar().blips[handleArg(args, 0)] = hud::RadarBlip{.type = 1};
                        }));
    // @orig 0x001b4168 HUD_RadarAddHuman (unknown)
    // @orig 0x00370a68 HUDAddRadarHuman (unknown)
    vm.registerFunction("HUDAddRadarHuman", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.radar().blips[handleArg(args, 0)] = hud::RadarBlip{.type = 7};
                        }));
    // @orig 0x001b4098 HUD_RadarRemove (unknown)
    const auto remove = [](hud::Hud& hud, std::span<const Value> args) { hud.radar().blips.erase(handleArg(args, 0)); };
    // @orig 0x00370980 HUDDeleteRadarMissionObjective (unknown)
    vm.registerFunction("HUDDeleteRadarMissionObjective", hudCall(context, remove));
    // @orig 0x00370a30 HUDDeleteRadarObject (unknown)
    vm.registerFunction("HUDDeleteRadarObject", hudCall(context, remove));
    // @orig 0x001b4038 HUD_RadarSetIcon (unknown)
    // @orig 0x003708c8 HUDSetRadarItemTexture (unknown)
    vm.registerFunction("HUDSetRadarItemTexture", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud::RadarBlip& blip = blipOf(hud, handleArg(args, 0), 10);
                            blip.icon = static_cast<int>(unsignedArg(args, 1));
                            blip.scale = floatArg(args, 2, 1.0F);
                        }));
    // @orig 0x001b4298 HUD_RadarFlash (unknown)
    // @orig 0x00370690 HUDSetRadarObjectFlash (unknown)
    vm.registerFunction("HUDSetRadarObjectFlash", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            const auto found = hud.radar().blips.find(handleArg(args, 0));
                            if (found != hud.radar().blips.end()) {
                                found->second.flashing = boolArg(args, 1);
                            }
                        }));
}

} // namespace

// The colour table {r, g, b} at argument `i` (each 0-255), opaque; white when it is not a table.
graphics::Rgba colourArg(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].table() == nullptr) {
        return graphics::kWhite;
    }
    const Table& table = *args[i].table();
    const auto channel = [&table](double k) {
        const double value = table.get(Value(k)).number().value_or(0.0);
        return static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0));
    };
    return graphics::Rgba{channel(1.0), channel(2.0), channel(3.0), 255};
}

// The scoreboard and the stopwatch's display.
void addScoreBindings(LuaVm& vm, const BindingContext& context) {
    // `HUDEnableTextProgress(on, labels, count, slot)`: up to six labels; slot defaults to 1.
    // @orig 0x0036fa28 HUDEnableTextProgress (unknown)
    vm.registerFunction("HUDEnableTextProgress", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            std::vector<std::string> labels;
                            if (args.size() > 1 && args[1].table() != nullptr) {
                                for (std::size_t k = 1; k <= hud::kTextProgressRows; ++k) {
                                    const Value label = args[1].table()->get(Value(static_cast<double>(k)));
                                    labels.emplace_back(label.string().value_or(std::string_view{}));
                                }
                            }
                            hud.enableTextProgress(boolArg(args, 0), labels, unsignedArg(args, 2),
                                                   unsignedArg(args, 3, 1));
                        }));
    // `HUDSetTextProgress(label, value, {r, g, b}, slot)`: slot defaults to 1.
    // @orig 0x0036fb38 HUDSetTextProgress (unknown)
    vm.registerFunction("HUDSetTextProgress", hudCall(context, [](hud::Hud& hud, std::span<const Value> args) {
                            hud.setTextProgress(binding::string(args, 0), unsignedArg(args, 1), colourArg(args, 2),
                                                unsignedArg(args, 3, 1));
                        }));
    // `W_ShowStopWatch(show, label, warnMs)`: the warning window (10 s by default) armed while shown, 0 when hidden.
    // @orig 0x00423670 StopWatch_Show (unknown)
    vm.registerFunction("W_ShowStopWatch", [context = &context](std::span<const Value> args) {
        const bool show = boolArg(args, 0);
        if (context->state != nullptr) {
            context->state->player.stopWatch.setWarning(show ? intArg(args, 2, kStopWatchWarnMs) : 0);
        }
        if (context->hud != nullptr) {
            context->hud->showStopWatch(show, absent(args, 1) ? std::string{} : binding::string(args, 1));
        }
        return binding::none();
    });
}

void addHudBindings(LuaVm& vm, const BindingContext& context) {
    addScoreBindings(vm, context);
    addPanelBindings(vm, context);
    addMessageBindings(vm, context);
    addPanelAndArrowBindings(vm, context);
    addRadarBindings(vm, context);
}

hud::HudServices hudServicesOf(const BindingContext& context) {
    hud::HudServices services;
    if (const gui::GlobalStrings* strings = context.strings; strings != nullptr) {
        services.hudString = [strings](std::uint32_t id) { return std::string(strings->get(id)); };
        services.tutorialString = [strings](std::uint32_t id) {
            return std::string(strings->get(gui::StringTable::Tutorial, id));
        };
        services.announceString = [strings](std::uint32_t id) {
            return std::string(strings->get(gui::StringTable::Announce, id));
        };
    }
    services.hudColour = [recorded = context.recorded](int slot) { return recordedHudColour(recorded, slot); };
    services.sound.cueName = [recorded = context.recorded](int cue) { return recordedInterfaceSound(recorded, cue); };
    services.stopWatchTime = [state = context.state] { return state != nullptr ? state->player.stopWatch.time() : 0; };
    return services;
}

} // namespace coney::script
