// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "physics/contact.h"
#include "physics/human_body.h"
#include "physics/physics_world.h"
#include "physics/shapes.h"

// Made-up owners for the body pool and human body tests: each owner id has a record the test sets (position, scale,
// reach, bones, weights, answers), and every strike, weapon contact and ray the code asks about is recorded.

namespace coney::physics::test {

/// One made-up owner.
struct FakeOwner final : ShapeOwner {
    anim::Vec3 position;
    float scale = 1.0F;
    float reach = 0.0F;
    OwnerId held = kNoOwner;
    OwnerId target = kNoOwner;
    OwnerId grabPartner = kNoOwner;
    bool player = false;
    bool capsuleStrike = false;
    bool strikes = true;
    BodyId body = kNoBody;
    float weight = 82.0F;
    float pushWeight = 82.0F;
    std::uint64_t state = 0;
    std::uint32_t weaponAnswer = 0;
    /// What the capsule's pose reads; nothing: posed as an owner that is not a human (its size kept).
    std::optional<HumanCapsuleState> human;
    /// Bone transforms in the body's frame, by bone; a bone not listed has none.
    std::map<int, Transform> bones;

    [[nodiscard]] Transform transformNow() const override { return Transform{.position = position, .rotation = {}}; }
    [[nodiscard]] Transform transformStored() const override { return transformNow(); }
    [[nodiscard]] std::optional<Transform> boneTransform(int bone) const override {
        const auto found = bones.find(bone);
        return found == bones.end() ? std::nullopt : std::optional<Transform>(found->second);
    }
    [[nodiscard]] std::optional<HumanCapsuleState> humanCapsule() const override { return human; }
};

/// The owners of a test, by id, with a log of what the code asked.
class FakeOwners final : public HumanBodyOwners {
  public:
    /// The owner `id`, made on first use.
    FakeOwner& operator[](OwnerId id) {
        auto& slot = m_owners[id];
        if (!slot) {
            slot = std::make_unique<FakeOwner>();
        }
        return *slot;
    }

    struct Hit {
        OwnerId striker = kNoOwner;
        Contact contact;
    };
    std::vector<Hit> strikeLog; ///< Every Strike_Contact, in order.
    std::vector<Hit> weaponLog; ///< Every weapon handler call, in order.
    bool blocked = false;       ///< What every ray answers.

    [[nodiscard]] const ShapeOwner& shapeOwner(OwnerId owner) const override { return at(owner); }
    [[nodiscard]] float scale(OwnerId owner) const override { return at(owner).scale; }
    [[nodiscard]] float reach(OwnerId owner) const override { return at(owner).reach; }
    [[nodiscard]] OwnerId heldObject(OwnerId owner) const override { return at(owner).held; }
    [[nodiscard]] OwnerId target(OwnerId owner) const override { return at(owner).target; }
    [[nodiscard]] bool isPlayer(OwnerId owner) const override { return at(owner).player; }
    [[nodiscard]] BodyId bodyOf(OwnerId owner) const override { return at(owner).body; }
    [[nodiscard]] anim::Vec3 position(OwnerId owner) const override { return at(owner).position; }
    [[nodiscard]] float attribute(OwnerId owner, int which) const override {
        return which == kAttributePushWeightId ? at(owner).pushWeight : at(owner).weight;
    }
    [[nodiscard]] std::uint64_t state(OwnerId owner) const override { return at(owner).state; }
    [[nodiscard]] OwnerId grabPartner(OwnerId owner) const override { return at(owner).grabPartner; }
    [[nodiscard]] bool hitsWithCapsule(OwnerId owner) const override { return at(owner).capsuleStrike; }
    [[nodiscard]] bool strikes(OwnerId striker, OwnerId /*other*/) const override { return at(striker).strikes; }
    void strikeContact(OwnerId striker, const Contact& contact) override {
        strikeLog.push_back(Hit{.striker = striker, .contact = contact});
    }
    std::uint32_t weaponContact(OwnerId handler, const Contact& contact) override {
        weaponLog.push_back(Hit{.striker = handler, .contact = contact});
        return at(handler).weaponAnswer;
    }
    [[nodiscard]] bool rayBlocked(const StrikeRay& ray, std::uint32_t options) const override {
        m_rays.emplace_back(ray, options);
        return blocked;
    }
    /// The rays asked so far.
    [[nodiscard]] const std::vector<std::pair<StrikeRay, std::uint32_t>>& raysAsked() const { return m_rays; }

  private:
    static constexpr int kAttributePushWeightId = 7;

    [[nodiscard]] const FakeOwner& at(OwnerId id) const { return *m_owners.at(id); }

    std::map<OwnerId, std::unique_ptr<FakeOwner>> m_owners;
    mutable std::vector<std::pair<StrikeRay, std::uint32_t>> m_rays;
};

} // namespace coney::physics::test
