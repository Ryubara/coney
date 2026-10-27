// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A fake of the level's objects as the AI sees them (ai::ItemServices), for the steal and wreck goals' tests: store
// fronts, owned objects, panes and smash targets set by the test; a pick-up takes its object at once.

#include <algorithm>
#include <optional>
#include <vector>

#include "ai/brain.h"
#include "ai/item_services.h"
#include "ai/smash_goals.h"

namespace coney::test {

// The store's zone in these tests.
constexpr int kZone = 24;

// A level's objects as the AI sees them: store fronts and owned objects; a pick-up takes its object at once (it is
// gone, as a TYPE_SPECIAL taken for good), noting who took it and where he stood.
class FakeItems final : public ai::ItemServices {
  public:
    struct Object {
        double handle = 0;
        anim::Vec3 position{};
        int zone = kZone;
        bool gone = false;
    };
    struct Take {
        double taker = 0;
        double object = 0;
        anim::Vec3 standing{};
    };
    std::vector<ai::StoreFlag> storeFlags;
    std::vector<Object> objects;
    std::vector<Take> takes;
    std::vector<ai::PaneView> glass;      // what panes() lists, whatever the centre and radius
    std::vector<ai::SmashTarget> targets; // what smashTargets() lists, likewise
    ai::HeldUse use = ai::HeldUse::Other; // what heldUse() says of any held object
    std::vector<double> flying;           // the objects in flight

    [[nodiscard]] bool inFlight(double handle) const override { return std::ranges::contains(flying, handle); }

    [[nodiscard]] std::vector<ai::PaneView> panes(anim::Vec3 /*centre*/, float /*radius*/) const override {
        return glass;
    }
    [[nodiscard]] std::vector<ai::SmashTarget> smashTargets(anim::Vec3 /*centre*/, float /*radius*/) const override {
        return targets;
    }
    [[nodiscard]] ai::HeldUse heldUse(double /*handle*/) const override { return use; }
    [[nodiscard]] std::optional<anim::Vec3> objectPosition(double handle) const override {
        for (const ai::PaneView& pane : glass) {
            if (pane.handle == handle) {
                return pane.centre;
            }
        }
        for (const ai::SmashTarget& target : targets) {
            if (target.handle == handle) {
                return target.position;
            }
        }
        return itemPosition(handle);
    }

    [[nodiscard]] std::vector<ai::StoreFlag> stores() const override { return storeFlags; }
    [[nodiscard]] std::vector<ai::LooseItem> ownedItems(int zone) const override {
        std::vector<ai::LooseItem> items;
        for (const Object& object : objects) {
            if (object.zone == zone && !object.gone) {
                items.push_back({object.handle, object.position});
            }
        }
        return items;
    }
    [[nodiscard]] std::optional<anim::Vec3> itemPosition(double handle) const override {
        const Object* object = find(handle);
        return object != nullptr && !object->gone ? std::optional<anim::Vec3>(object->position) : std::nullopt;
    }
    [[nodiscard]] std::optional<double> holder(double /*handle*/) const override { return std::nullopt; }
    bool startPickUp(ai::Brain& brain, double handle) override {
        Object* object = find(handle);
        if (object == nullptr || object->gone) {
            return false;
        }
        object->gone = true;
        takes.push_back({brain.handle(), handle, brain.human().position()});
        return true;
    }

    // Adds an owned object with `handle` at `at`.
    void add(double handle, anim::Vec3 at) { objects.push_back({handle, at, kZone, false}); }

  private:
    // The object with `handle`; null for none.
    [[nodiscard]] Object* find(double handle) {
        for (Object& object : objects) {
            if (object.handle == handle) {
                return &object;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const Object* find(double handle) const { return const_cast<FakeItems*>(this)->find(handle); }
};

} // namespace coney::test
