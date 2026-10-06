// SPDX-License-Identifier: GPL-3.0-or-later
// The pages over a mode the player plays in: Player, Camera, Spawner, and Debug draw (docs/guides/debug-menu.md).
#include <cmath>
#include <cstddef>
#include <format>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "debug/debug_pages.h"
#include "debug/debug_session.h"
#include "debug/play_controls.h"

namespace coney::debug {

namespace {

// What a page shows in place of its items when no player plays.
constexpr std::string_view kNoPlayer = "no player: --play-level, or a sandbox from the Levels page";

// A point as the pages print it.
std::string pointText(anim::Vec3 p) { return std::format("({:.2f}, {:.2f}, {:.2f})", p.x, p.y, p.z); }

// A watch over the mode playing now: `text` of it, or the no-player line. The mode is looked up at each read, since
// it can change while the page is open.
MenuItem playWatch(DebugSession& session, std::string label, std::function<std::string(const PlayControls&)> text,
                   std::string channel = {}) {
    return watchItem(
        std::move(label),
        [&session, text = std::move(text)] {
            const PlayControls* play = session.play();
            return play != nullptr ? text(*play) : std::string("-");
        },
        std::move(channel));
}

// An action on the mode playing now, or a log line saying there is none.
MenuItem playAction(DebugSession& session, std::string label, std::function<void(PlayControls&)> act) {
    return actionItem(std::move(label), [&session, act = std::move(act)] {
        PlayControls* play = session.play();
        if (play == nullptr) {
            session.print(std::string(kNoPlayer));
            return;
        }
        act(*play);
    });
}

// A toggle over the mode playing now; off, and inert, when there is none.
MenuItem playToggle(DebugSession& session, std::string label, std::function<bool(const PlayControls&)> get,
                    std::function<void(PlayControls&, bool)> set) {
    return toggleItem(
        std::move(label),
        [&session, get = std::move(get)] {
            const PlayControls* play = session.play();
            return play != nullptr && get(*play);
        },
        [&session, set = std::move(set)](bool on) {
            if (PlayControls* play = session.play(); play != nullptr) {
                set(*play, on);
            } else {
                session.print(std::string(kNoPlayer));
            }
        });
}

// The menu callbacks below copy strings and call through std::function; all they can throw is a failed allocation,
// which ends the program either way.
// NOLINTBEGIN(bugprone-exception-escape)

// The Teleport page: the scene's places, a spot typed in, and a saved spot.
void fillTeleport(MenuPage& page, DebugSession& session) {
    PlayControls* play = session.play();
    if (play == nullptr) {
        page.add(watchItem("No player", [] { return std::string(kNoPlayer); }));
        return;
    }
    for (const Place& place : play->places()) {
        page.add(playAction(session, place.name, [&session, place](PlayControls& p) {
                     p.teleport(place);
                     session.print(std::format("player: to {} {}", place.name, pointText(place.feet)));
                 }).withHelp(std::format("Feet {}, heading {:.0f}.", pointText(place.feet), place.headingDegrees)));
    }
    // A spot typed in, starting where the player stands.
    auto typed = std::make_shared<Place>(Place{"typed spot", play->playerFeet(), play->playerHeadingDegrees()});
    const auto coordinate = [typed](std::string label, float* value) {
        MenuItem item = numberItem(
            std::move(label), [value] { return static_cast<double>(*value); },
            [value](double v) { *value = static_cast<float>(v); }, -5000.0, 5000.0, 0.5, false);
        item.units = "m";
        item.keepAlive = typed;
        return item;
    };
    page.add(coordinate("X", &typed->feet.x));
    page.add(coordinate("Y", &typed->feet.y));
    page.add(coordinate("Z", &typed->feet.z));
    MenuItem heading = numberItem(
        "Heading", [typed] { return static_cast<double>(typed->headingDegrees); },
        [typed](double v) { typed->headingDegrees = static_cast<float>(v); }, -180.0, 180.0, 5.0, false);
    heading.units = "deg";
    heading.keepAlive = typed;
    page.add(std::move(heading));
    page.add(playAction(session, "Go to X Y Z", [&session, typed](PlayControls& p) {
                 p.teleport(*typed);
                 session.print(std::format("player: to {}", pointText(typed->feet)));
             }).withHelp("Puts the player at the spot above, dropped onto the ground below it."));
}

// The Change character items: the type to become (a choice over the configuration's types, starting at the player's
// own) and the action that rebuilds the player as it. `chosen` keeps the type picked, shared by every build of the
// page and by pinned copies.
void addCharacterItems(MenuPage& page, DebugSession& session, PlayControls& play,
                       const std::shared_ptr<std::optional<int>>& chosen) {
    page.add(playWatch(session, "Character", [](const PlayControls& p) { return p.characterState(); }));
    auto choices = std::make_shared<const std::vector<CharacterChoice>>(play.characterChoices());
    if (choices->empty()) {
        page.add(watchItem("Character type", [] { return std::string("none: no CfgChar configuration here"); }));
        return;
    }
    // The index of type `type` among the choices; the first when it is not one of them.
    const auto indexOf = [choices](int type) {
        for (std::size_t i = 0; i < choices->size(); ++i) {
            if ((*choices)[i].type == type) {
                return i;
            }
        }
        return std::size_t{0};
    };
    std::vector<std::string> labels;
    labels.reserve(choices->size());
    for (const CharacterChoice& choice : *choices) {
        labels.push_back(std::format("{} {}", choice.type, choice.model));
    }
    MenuItem type = choiceItem(
        "Character type", std::move(labels),
        [&session, chosen, indexOf] {
            const PlayControls* p = session.play();
            return indexOf(chosen->value_or(p != nullptr ? p->playerType() : 0));
        },
        [chosen, choices](std::size_t index) {
            if (index < choices->size()) {
                *chosen = (*choices)[index].type;
            }
        });
    type.keepAlive = choices;
    page.add(std::move(type))
        .withHelp("A character type of the game's configuration (CfgChar), with the model a player of it is drawn as.");
    page.add(playAction(session, "Change character",
                        [&session, chosen](PlayControls& p) {
                            const int wanted = chosen->value_or(p.playerType());
                            auto changed = p.changeCharacter(wanted);
                            session.print(changed
                                              ? std::format("player: now {}", p.characterState())
                                              : std::format("player: type {}: {}", wanted, changed.error().message));
                        }))
        .withHelp("Rebuilds the player as the type above where he stands, at full health, the camera behind him.");
}

} // namespace

void addPlayerPage(DebugSession& session) {
    session.model().addChannel("Player/Speed", [&session] {
        const PlayControls* play = session.play();
        return play != nullptr ? play->playerSpeed() : 0.0F;
    });
    session.model().addChannel("Player/Height", [&session] {
        const PlayControls* play = session.play();
        return play != nullptr ? play->playerFeet().z : 0.0F;
    });
    // The spot Save remembers, shared by every build of the page.
    auto saved = std::make_shared<std::optional<Place>>();
    // The character type Change character makes him: none picked yet means his own.
    auto chosenType = std::make_shared<std::optional<int>>();
    session.model().addPage(
        "Player",
        [&session, saved, chosenType](MenuPage& page) {
            if (session.play() == nullptr) {
                page.add(watchItem("No player", [] { return std::string(kNoPlayer); }));
                return;
            }
            page.add(playWatch(session, "Scene", [](const PlayControls& p) { return p.sceneName(); }));
            page.add(playWatch(
                session, "Feet", [](const PlayControls& p) { return pointText(p.playerFeet()); }, "Player/Height"));
            page.add(playWatch(session, "Heading", [](const PlayControls& p) {
                return std::format("{:.1f} deg", p.playerHeadingDegrees());
            }));
            page.add(playWatch(
                session, "Speed", [](const PlayControls& p) { return std::format("{:.2f} m/s", p.playerSpeed()); },
                "Player/Speed"));
            page.add(playWatch(session, "Movement", [](const PlayControls& p) { return p.playerState(); }));
            addCharacterItems(page, session, *session.play(), chosenType);
            page.add(playToggle(
                         session, "Frozen", [](const PlayControls& p) { return p.playerFrozen(); },
                         [](PlayControls& p, bool on) { p.setPlayerFrozen(on); }))
                .withHelp("The player ignores the pad and stands still.");
            page.add(submenuItem("Teleport", [&session] {
                         auto sub = std::make_shared<MenuPage>("Teleport");
                         fillTeleport(*sub, session);
                         return sub;
                     }).withHelp("To the scene's places or a spot typed in."));
            page.add(playAction(session, "Save this spot",
                                [&session, saved](PlayControls& p) {
                                    *saved = Place{"saved spot", p.playerFeet(), p.playerHeadingDegrees()};
                                    session.print(std::format("player: saved {}", pointText(p.playerFeet())));
                                }))
                .withHelp("Remembers where the player stands, for Back to saved spot.");
            page.add(playAction(session, "Back to saved spot", [&session, saved](PlayControls& p) {
                if (!saved->has_value()) {
                    session.print("player: no spot saved yet");
                    return;
                }
                p.teleport(saved->value());
            }));
            page.add(logItem("Log", [&session] { return session.log().last(3); }));
        },
        "The player: where, how fast, his character type, frozen, teleports.");
}

void addCameraPage(DebugSession& session) {
    session.model().addChannel("Camera/Distance", [&session] {
        const PlayControls* play = session.play();
        return play != nullptr ? anim::distance(play->cameraEye(), play->cameraTarget()) : 0.0F;
    });
    session.model().addPage(
        "Camera",
        [&session](MenuPage& page) {
            if (session.play() == nullptr) {
                page.add(watchItem("No player", [] { return std::string(kNoPlayer); }));
                return;
            }
            page.add(playWatch(session, "Eye", [](const PlayControls& p) { return pointText(p.cameraEye()); }));
            page.add(playWatch(session, "Target", [](const PlayControls& p) { return pointText(p.cameraTarget()); }));
            page.add(playWatch(
                session, "Distance",
                [](const PlayControls& p) {
                    return std::format("{:.2f} m", anim::distance(p.cameraEye(), p.cameraTarget()));
                },
                "Camera/Distance"));
            page.add(playAction(session, "Reset behind player", [](PlayControls& p) { p.resetCamera(); }))
                .withHelp("Places the follow camera again with the Follow camera values below.");
            page.add(playToggle(
                         session, "Free camera", [](const PlayControls& p) { return p.freeCamera(); },
                         [](PlayControls& p, bool on) { p.setFreeCamera(on); }))
                .withHelp("Pad 1 flies the view (left stick, right stick, L1/R1); the player stands still.");
            page.add(submenuItem("Follow camera values", [&session] {
                         auto sub = std::make_shared<MenuPage>("Follow camera values");
                         sub->setRebuild(
                             [&session](MenuPage& p) { fillTunableCategory(p, session.tunables(), "Follow camera"); });
                         return sub;
                     }).withHelp("The Follow camera tunables; Reset behind player applies the placement ones."));
        },
        "The follow camera, its reset and values, and the free camera.");
}

void addSpawnerPage(DebugSession& session) {
    auto distance = std::make_shared<double>(3.0);
    session.model().addPage(
        "Spawner",
        [&session, distance](MenuPage& page) {
            PlayControls* play = session.play();
            if (play == nullptr) {
                page.add(watchItem("No player", [] { return std::string(kNoPlayer); }));
                return;
            }
            if (!play->canSpawn()) {
                page.add(watchItem("Spawning", [] { return std::string("needs a sandbox: load one from Levels"); }));
                return;
            }
            MenuItem ahead = numberItem(
                "Distance ahead", [distance] { return *distance; }, [distance](double v) { *distance = v; }, 1.0, 20.0,
                0.5, false);
            ahead.units = "m";
            ahead.defaultValue = 3.0;
            page.add(std::move(ahead));
            for (const Spawnable& spawnable : spawnables()) {
                page.add(
                    playAction(session, std::string(spawnable.name), [&session, distance, spawnable](PlayControls& p) {
                        sandbox::Primitive primitive = spawnable.primitive;
                        primitive.base =
                            spotAhead(p.playerFeet(), p.playerHeadingDegrees(), static_cast<float>(*distance));
                        primitive.yawDegrees = p.playerHeadingDegrees();
                        auto spawned = p.spawn(primitive);
                        session.print(spawned
                                          ? std::format("spawner: {} at {}", spawnable.name, pointText(primitive.base))
                                          : "spawner: " + spawned.error().message);
                    }));
            }
            page.add(playWatch(session, "Spawned",
                               [](const PlayControls& p) { return std::format("{}", p.spawnedCount()); }));
            page.add(playAction(session, "Clear spawned", [&session](PlayControls& p) {
                auto cleared = p.clearSpawned();
                session.print(cleared ? std::string("spawner: cleared") : "spawner: " + cleared.error().message);
            }));
            page.add(logItem("Log", [&session] { return session.log().last(3); }));
        },
        "Objects put in front of the player, in a sandbox.");
}

void addFightersPage(DebugSession& session) {
    auto distance = std::make_shared<double>(4.0);
    session.model().addPage(
        "AI fighters",
        [&session, distance](MenuPage& page) {
            const PlayControls* play = session.play();
            if (play == nullptr) {
                page.add(watchItem("No player", [] { return std::string(kNoPlayer); }));
                return;
            }
            if (!play->canSpawnFighter()) {
                page.add(watchItem("Fighters", [] { return std::string("none in this mode"); }));
                return;
            }
            MenuItem ahead = numberItem(
                "Distance ahead", [distance] { return *distance; }, [distance](double v) { *distance = v; }, 1.0, 20.0,
                0.5, false);
            ahead.units = "m";
            ahead.defaultValue = 4.0;
            page.add(std::move(ahead));
            page.add(playAction(session, "Spawn a fighter",
                                [&session, distance](PlayControls& p) {
                                    const anim::Vec3 feet = spotAhead(p.playerFeet(), p.playerHeadingDegrees(),
                                                                      static_cast<float>(*distance));
                                    // It faces the player.
                                    auto spawned = p.spawnFighter(feet, p.playerHeadingDegrees() + 180.0F);
                                    session.print(spawned ? std::format("fighters: one at {}", pointText(feet))
                                                          : "fighters: " + spawned.error().message);
                                }))
                .withHelp("An AI human with a sparring Warrior's brain, facing the player; it fights him when he is "
                          "in its melee range and engaging is on.");
            page.add(playToggle(
                         session, "Engaging", [](const PlayControls& p) { return p.fightersEngage(); },
                         [](PlayControls& p, bool on) { p.setFightersEngage(on); }))
                .withHelp("An idle fighter takes the player on when he comes within its melee range (the level "
                          "script's GoalFight stand-in).");
            page.add(playWatch(session, "Fighters",
                               [](const PlayControls& p) { return std::format("{}", p.fighterCount()); }));
            page.add(playWatch(session, "State", [](const PlayControls& p) { return p.fightersState(); }));
            page.add(playAction(session, "Clear fighters", [&session](PlayControls& p) {
                p.clearFighters();
                session.print("fighters: cleared");
            }));
            page.add(logItem("Log", [&session] { return session.log().last(3); }));
        },
        "AI humans that fight the player: spawn, engage, watch.");
}

void addDebugDrawPage(DebugSession& session) {
    DebugDrawOptions& draw = session.debugDraw();
    session.model().addPage(
        "Debug draw",
        [&draw](MenuPage& page) {
            const auto toggle = [](std::string label, bool& flag, std::string help) {
                return toggleItem(
                           std::move(label), [&flag] { return flag; }, [&flag](bool on) { flag = on; })
                    .withHelp(std::move(help));
            };
            page.add(toggle("Collision", draw.collision, "The collision triangles near the player, as a wireframe."));
            MenuItem radius = numberItem(
                "Collision radius", [&draw] { return static_cast<double>(draw.collisionRadius); },
                [&draw](double v) { draw.collisionRadius = static_cast<float>(v); }, 1.0, 50.0, 1.0, false);
            radius.units = "m";
            radius.defaultValue = 8.0;
            page.add(std::move(radius));
            page.add(toggle("Player", draw.player, "The player's feet, heading and velocity."));
            page.add(toggle("Ground normal", draw.groundNormal, "The normal of the ground under the player."));
            page.add(toggle("Camera", draw.camera, "The follow camera's wanted position and look-at point."));
            page.add(toggle("Places", draw.places, "The scene's places (start, spawn points) with their headings."));
        },
        "Lines drawn into the scene: collision, player, camera, places.");
}

// NOLINTEND(bugprone-exception-escape)

const std::vector<Spawnable>& spawnables() {
    // Coney's own sizes: a crate to jump on, a fence to mount, a wall, a kerb ramp, a short flight of stairs, a pillar
    // and a ball, each tinted so they stand out from a layout's own primitives.
    static const std::vector<Spawnable> kSpawnables = [] {
        const auto make = [](sandbox::Shape shape, anim::Vec3 size, sandbox::Colour tint) {
            sandbox::Primitive p;
            p.shape = shape;
            p.size = size;
            p.tint = tint;
            return p;
        };
        std::vector<Spawnable> list;
        list.push_back({"Crate (1 m)", make(sandbox::Shape::Box, {1.0F, 1.0F, 1.0F}, {0.9F, 0.7F, 0.4F})});
        list.push_back({"Fence (1.2 m)", make(sandbox::Shape::Box, {3.0F, 0.1F, 1.2F}, {0.5F, 0.8F, 0.5F})});
        list.push_back({"Wall (2.5 m)", make(sandbox::Shape::Box, {4.0F, 0.3F, 2.5F}, {0.8F, 0.5F, 0.5F})});
        list.push_back({"Ramp (1 m rise)", make(sandbox::Shape::Ramp, {2.0F, 3.0F, 1.0F}, {0.6F, 0.6F, 0.9F})});
        sandbox::Primitive stairs = make(sandbox::Shape::Stairs, {2.0F, 0.0F, 0.0F}, {0.7F, 0.7F, 0.7F});
        stairs.steps = 6;
        stairs.size = {2.0F, 1.8F, 1.08F}; // six steps of 0.18 m rise and 0.3 m run
        list.push_back({"Stairs (6 steps)", stairs});
        sandbox::Primitive pillar = make(sandbox::Shape::Cylinder, {0.8F, 0.8F, 3.0F}, {0.9F, 0.9F, 0.6F});
        pillar.radius = 0.4F;
        pillar.segments = 16;
        list.push_back({"Pillar (3 m)", pillar});
        sandbox::Primitive ball = make(sandbox::Shape::Sphere, {1.5F, 1.5F, 1.5F}, {0.4F, 0.8F, 0.9F});
        ball.radius = 0.75F;
        ball.segments = 16;
        list.push_back({"Ball (1.5 m)", ball});
        return list;
    }();
    return kSpawnables;
}

anim::Vec3 spotAhead(anim::Vec3 feet, float headingDegrees, float distance) {
    // Heading 0 faces +y and grows counter-clockwise, as the human's facing does.
    const float radians = headingDegrees * std::numbers::pi_v<float> / 180.0F;
    return anim::Vec3{feet.x - std::sin(radians) * distance, feet.y + std::cos(radians) * distance, feet.z};
}

} // namespace coney::debug
