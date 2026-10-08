// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A synthetic scene for the scripted goals, the gangs, the formations and the tactics: the fighting character's humans
// on an 80 m floor, each with a brain, stepped through the characters' step; and script services that keep what the
// goals and gangs ask of the script system and play every clip for a set number of updates.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ai/ai_config.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/script_services.h"
#include "animation/anim_math.h"
#include "human/human.h"
#include "human/humans.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

namespace coney::test {

/// One call the services were asked for: the function, its arguments, and the delay (0 for a call now).
struct ScriptCall {
    std::string function;
    std::vector<double> args;
    std::uint32_t delayMs = 0;
    bool scheduled = false;
};

/// Script services over a map of handles that keep every schedule and call, and the clips played.
class KeepingServices final : public ai::ScriptServices {
  public:
    [[nodiscard]] ai::Brain* brain(double handle) const override {
        const auto found = brains.find(handle);
        return found == brains.end() ? nullptr : found->second;
    }
    [[nodiscard]] ai::Brain* player() const override { return playerBrain; }
    void schedule(std::string_view function, std::span<const double> args, std::uint32_t delayMs) override {
        calls.push_back(ScriptCall{std::string(function), {args.begin(), args.end()}, delayMs, true});
    }
    bool call(std::string_view function, std::span<const double> args) override {
        calls.push_back(ScriptCall{std::string(function), {args.begin(), args.end()}, 0, false});
        return callResult;
    }
    bool humanEvent(ai::Brain& human, const ai::BrainEvent& event) override {
        humanEvents.emplace_back(&human, event);
        return humanEventTaken;
    }
    void playScene(int scene, ai::Brain& /*human*/, std::string_view callback) override {
        scenes.emplace_back(scene, std::string(callback));
    }
    [[nodiscard]] bool sceneFinished(int /*scene*/) const override { return sceneOver; }
    void stopScene(int scene) override { stopped.push_back(scene); }
    void loadDynamicClip(ai::Brain& /*human*/, std::string_view name) override { loaded.emplace_back(name); }
    void freeDynamicClip(ai::Brain& /*human*/) override { ++freed; }
    void addRadarIcon(ai::Brain& human, int type, int icon, float factor) override {
        radarIcons.push_back(RadarIcon{&human, type, icon, factor});
    }
    [[nodiscard]] std::optional<std::uint32_t> playClip(ai::Brain& /*human*/, int animId) override {
        clips.push_back(animId);
        if (!clipsPlay) {
            return std::nullopt;
        }
        return 0U; // held by no flag: the action ends on its next update
    }
    [[nodiscard]] std::optional<std::uint32_t> playNamedClip(ai::Brain& human, int animId, std::string_view name,
                                                             float /*fade*/) override {
        namedClips.emplace_back(&human, animId, std::string(name));
        return 0U;
    }
    [[nodiscard]] bool clipAvailable(std::string_view /*name*/) const override { return clipsAvailable; }
    void say(ai::Brain& human, int command, bool /*interrupt*/, double /*target*/) override {
        said.emplace_back(&human, static_cast<std::uint32_t>(command));
    }

    /// One addRadarIcon() call.
    struct RadarIcon {
        ai::Brain* human;
        int type;
        int icon;
        float factor;
    };

    std::map<double, ai::Brain*> brains;
    ai::Brain* playerBrain = nullptr;
    std::vector<RadarIcon> radarIcons;
    std::vector<ScriptCall> calls;
    bool callResult = false;
    std::vector<std::pair<ai::Brain*, ai::BrainEvent>> humanEvents; // offered to a human's own handlers
    bool humanEventTaken = false;
    std::vector<std::pair<int, std::string>> scenes;
    bool sceneOver = false;
    std::vector<int> stopped;
    std::vector<std::string> loaded;
    int freed = 0;
    std::vector<int> clips;
    bool clipsPlay = true;
    std::vector<std::tuple<ai::Brain*, int, std::string>> namedClips; // (human, anim id, clip) played by name
    bool clipsAvailable = false;                                      // what clipAvailable() answers
    std::vector<std::pair<ai::Brain*, std::uint32_t>> said;           // (human, speech command)
};

/// Humans on a floor, each with a brain; slot 0 is the pad's player at (40, 40) facing +y.
struct AiScene {
    FightCharacter character;
    std::unique_ptr<raycast::CollisionMesh> mesh = makeMesh(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    std::vector<std::unique_ptr<human::Human>> humans;
    KeepingServices services; // before the brains, whose goals may call it as they go
    human::Humans step;
    ai::Brains brains;

    AiScene() {
        add(anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F, ai::BrainType::Player);
        services.playerBrain = &brains.at(0);
        step.setBrains(brains.hook());
        brains.gangs().setScripts(&services);
    }

    /// A human with its feet at `feet` facing `headingDegrees` (0 faces +y), with a brain of `type`; its handle is its
    /// slot + 1, known to the services.
    ai::Brain& add(anim::Vec3 feet, float headingDegrees, ai::BrainType type = ai::BrainType::Gang) {
        humans.push_back(std::make_unique<human::Human>(character.anims, human::AnimSlots::player(), identityBind(),
                                                        1.0F, &character.ranges));
        human::Human& made = *humans.back();
        const bool player = type == ai::BrainType::Player;
        made.setFighterProfile(
            human::FighterProfile{.player = player, .powerClass = ai::kWarriorPowerClass, .health = 1400});
        made.spawn(mesh.get(), feet, headingDegrees);
        step.add(made, player);
        ai::Brain& brain = brains.add(made, type, ai::FightSettings{}, static_cast<std::uint32_t>(humans.size()));
        brain.setHandle(static_cast<double>(humans.size()));
        services.brains[brain.handle()] = &brain;
        return brain;
    }

    ai::Brain& player() { return brains.at(0); }
    void run(int steps) {
        for (int k = 0; k < steps; ++k) {
            step.update(mesh.get());
        }
    }
};

} // namespace coney::test
