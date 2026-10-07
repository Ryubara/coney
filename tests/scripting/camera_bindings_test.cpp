// SPDX-License-Identifier: GPL-3.0-or-later
// The camera bindings level99 calls (docs/research/camera.md#script-calls): global.lua's CameraCreateFollow and
// level99's AddCameras, a tutorial cut-away and the blend back, CamSetFollowZoom, CamSetFollowAngle, CamEnable,
// CamTarget and CamSetSecondary, and the same calls with no cameras.
#include "scripting/camera_bindings.h"

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/cameras.h"
#include "camera/follow_camera.h"
#include "core/error.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

using Catch::Approx;
using coney::camera::CameraKind;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Table;
using coney::script::Value;

namespace {

// A host that ignores every request: these bindings ask nothing of it.
class QuietHost final : public coney::script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
};

// A script system with Coney's bindings over player 1's cameras (or none).
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::world_objects::WorldFlags flags;
    coney::camera::FollowCamera follow{coney::anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F};
    coney::camera::Cameras cameras;
    coney::script::BindingContext context{&state, &strings, &host, &recorded, &humans, &flags};
    ScriptSystem scripts;

    explicit Harness(bool withCameras = true)
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        if (withCameras) {
            cameras.attachFollow(&follow);
            context.cameras = &cameras;
        }
        // Gameplay gives the locator; here the humans' placements stand in for their live positions.
        cameras.setLocator([this](double handle) -> std::optional<coney::anim::Vec3> {
            const std::optional<coney::world_objects::Placement> placement = humans.placement(handle);
            if (!placement) {
                return std::nullopt;
            }
            return coney::anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
        });
        scripts.create();
    }

    // The first result of `name(args)`, REQUIRing success; nil when there is none.
    Value call(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result && !result->empty() ? (*result)[0] : Value();
    }
};

// A Lua string value.
Value str(const char* text) { return Value(std::string(text)); }

// A table {a, b, c}.
Value triple(double a, double b, double c) {
    auto table = std::make_shared<Table>();
    REQUIRE(table->set(Value(1.0), Value(a)).has_value());
    REQUIRE(table->set(Value(2.0), Value(b)).has_value());
    REQUIRE(table->set(Value(3.0), Value(c)).has_value());
    return Value(table);
}

} // namespace

TEST_CASE("CameraCreateFollow's calls set the follow camera up with level99's band, and a cut-away blends back",
          "[camera_bindings]") {
    Harness h;
    // global.lua's CameraCreateFollow("follow", player), then AddCameras' CameraMakeActive and CameraReset.
    const double follow = h.call("CamSetupFollow", {str("follow"), Value(1.0)}).number().value_or(0.0);
    CHECK(follow >= 1.0);
    CHECK(h.call("CamSetupFollow", {str("follow"), Value(1.0)}).number() == follow);
    h.call("CfgFollowCamera",
           {Value(3.0), Value(6.6), Value(4.8), Value(13.0), Value(65.0), Value(0.1), triple(0, 0, 1.4), Value(0.2)});
    h.call("CameraMakeActive", {Value(follow), Value(0.0)});
    h.call("CameraReset", {Value(follow)});
    CHECK(h.follow.bandNear() == Approx(3.0F));
    CHECK(h.follow.bandFar() == Approx(3.5F));
    CHECK(h.follow.zoomDistance() == Approx(4.8F));
    CHECK(h.cameras.slowMotion().factor() == Approx(0.2F));
    CHECK(h.cameras.current().kind == CameraKind::Follow);
    // A cut-away: a locked camera made current at once, then back with a 1 s blend.
    const double cut = h.call("CameraCreateLocked", {str("VerminCar"), triple(10, 20, 3), Value(50.0), Value(90.0),
                                                     Value(10.0), Value(0.0), Value(0.1), Value(100.0)})
                           .number()
                           .value_or(0.0);
    CHECK(cut == follow + 1.0);
    REQUIRE(h.cameras.locked(cut) != nullptr);
    CHECK(h.cameras.locked(cut)->fieldOfView == 50.0F);
    CHECK(h.cameras.locked(cut)->headingDegrees == 90.0F);
    CHECK(h.cameras.locked(cut)->farClip == 100.0F);
    h.call("CameraMakeActive", {Value(cut), Value(0.0)});
    CHECK(h.cameras.current().kind == CameraKind::Locked);
    h.call("CameraReset", {Value(follow)});
    h.call("CameraMakeActive", {Value(follow), Value(1.0)});
    CHECK(h.cameras.current().kind == CameraKind::Follow);
    CHECK(h.cameras.blending());
}

TEST_CASE("the tutorial's follow camera calls: zoom, angle, switches, targets and the watched human",
          "[camera_bindings]") {
    Harness h;
    const double follow = h.call("CamSetupFollow", {str("follow"), Value(1.0)}).number().value_or(0.0);
    h.call("CfgFollowCamera",
           {Value(3.0), Value(6.6), Value(4.8), Value(13.0), Value(65.0), Value(0.1), triple(0, 0, 1.4), Value(0.2)});
    h.call("CamSetFollowZoom", {Value(1.0)});
    CHECK(h.follow.bandNear() == Approx(4.8F));
    h.call("CamSetFollowZoom", {Value(7.0)}); // not a preset: nothing
    CHECK(h.follow.bandNear() == Approx(4.8F));
    h.call("CamSetFollowAngle", {Value(-10.0)});
    CHECK(h.follow.targetPitch() / (3.14159265F / 180.0F) == Approx(-4.32F).margin(0.01));
    h.call("CamEnable", {Value(0.0), Value()});
    CHECK_FALSE(h.cameras.enabled(0));
    h.call("CamEnable", {Value(0.0), Value(1.0)});
    CHECK(h.cameras.enabled(0));
    CHECK(h.call("CamTarget", {Value(0.0), Value(follow), Value(5.0)}).number() == 1.0);
    CHECK(h.call("CamTarget", {Value(1.0), Value(follow), Value(5.0)}).number() == 1.0);
    CHECK(h.cameras.targets().empty());
    // CamSetSecondary finds the human through the scripts' humans.
    const double vermin = h.call("HuCreate", {str("Vermin"), Value(1.0), triple(50, 40, 0)}).number().value_or(0.0);
    h.call("CamSetSecondary", {Value(vermin), Value(0.0), Value(0.0)});
    CHECK(h.cameras.secondaryPoint().value_or(coney::anim::Vec3{}).x == 50.0F);
    h.call("CamSetSecondary", {Value(0.0), Value(0.0), Value(0.0)});
    CHECK_FALSE(h.cameras.secondaryPoint().has_value());
    // The split-screen layout: 0 or 1, other values ignored.
    h.call("CamSetSplitMode", {Value(1.0)});
    h.call("CamSetSplitMode", {Value(4.0)});
    CHECK(h.cameras.splitMode() == 1);
}

TEST_CASE("with no cameras the making bindings still return handles and the rest do nothing", "[camera_bindings]") {
    Harness h(false);
    const double follow = h.call("CamSetupFollow", {str("follow"), Value(1.0)}).number().value_or(0.0);
    CHECK(follow >= 1.0);
    const double cut = h.call("CameraCreateLocked", {str("c"), triple(0, 0, 0), Value(50.0)}).number().value_or(0.0);
    CHECK(cut == follow + 1.0);
    h.call("CameraMakeActive", {Value(cut), Value(1.0)});
    h.call("CfgFollowCamera", {Value(3.0), Value(6.6), Value(4.8)});
    CHECK(h.call("CamTarget", {Value(0.0), Value(follow), Value(5.0)}).isNil());
}

TEST_CASE("cameras given after the state was made are the ones the bindings drive", "[camera_bindings]") {
    // Gameplay makes each level's cameras after the scripts' state, and drops them when the level ends.
    Harness h(false);
    h.cameras.attachFollow(&h.follow);
    h.context.cameras = &h.cameras;
    const double follow = h.call("CamSetupFollow", {str("follow"), Value(1.0)}).number().value_or(0.0);
    CHECK(h.cameras.followHandle() == follow);
    h.call("CamSetFollowZoom", {Value(1.0)});
    CHECK(h.follow.bandNear() == Approx(4.8F));
    h.context.cameras = nullptr;
    h.call("CamSetFollowZoom", {Value(2.0)});
    CHECK(h.follow.bandNear() == Approx(4.8F));
}

TEST_CASE("CamLockLocked lists a human on a locked camera and takes him off again", "[camera_bindings]") {
    Harness h;
    const double cut = h.call("CameraCreateLocked", {str("introCam"), triple(10, 20, 3), Value(50.0), Value(90.0)})
                           .number()
                           .value_or(0.0);
    h.call("CamLockLocked", {Value(cut), Value(12.0), Value(1.0)});
    h.call("CamLockLocked", {Value(cut + 100.0), Value(13.0), Value(1.0)}); // not a camera: nothing
    REQUIRE(h.cameras.locked(cut) != nullptr);
    CHECK(h.cameras.locked(cut)->keptInView == std::vector<double>{12.0});
    h.call("CamLockLocked", {Value(cut), Value(12.0), Value()});
    CHECK(h.cameras.locked(cut)->keptInView.empty());
}

TEST_CASE("a chase's rail camera calls: set-up, points, lead and settings; fixed and third-person cameras",
          "[camera_bindings]") {
    Harness h;
    // A point before any rail camera is not added; a NilHandle target makes none.
    CHECK(h.call("CamAddRailPoint", {triple(0, 0, 2)}).isNil());
    CHECK(h.call("CamSetupRail", {str("rail"), Value(0.0), Value(60.0), triple(0, 0, 1.5), Value(0.1), Value(90.0)})
              .number() == 0.0);
    const double rail =
        h.call("CamSetupRail", {str("rail"), Value(5.0), Value(60.0), triple(0, 0, 1.5), Value(0.1), Value(90.0)})
            .number()
            .value_or(0.0);
    CHECK(rail >= 1.0);
    CHECK(h.call("CamAddRailPoint", {triple(0, 0, 2)}).number() == 1.0);
    CHECK(h.call("CamAddRailPoint", {triple(10, 0, 2), Value(1.0)}).isNil()); // player 2 has no rail camera
    h.call("CamLeadRail", {Value(3.0), Value(0.0)});
    h.call("CamModifyRail", {Value(2.0), Value(40.0), Value(0.0)});
    REQUIRE(h.cameras.rail() != nullptr);
    CHECK(h.cameras.rail()->pointCount() == 1);
    CHECK(h.cameras.rail()->mode() == coney::camera::RailMode::Ahead);
    CHECK(h.cameras.rail()->lead() == Approx(3.0F));
    CHECK(h.cameras.rail()->fieldOfView() == Approx(40.0F));
    // CamSetupRail again keeps the handle.
    CHECK(h.call("CamSetupRail", {str("rail"), Value(5.0), Value(60.0), triple(0, 0, 1.5), Value(0.1), Value(90.0)})
              .number() == rail);

    const double fixed = h.call("CameraCreateFixed", {str("fixed"), Value(5.0), triple(0, 0, 5), Value(50.0),
                                                      triple(0, 0, 1), Value(0.1), Value(100.0)})
                             .number()
                             .value_or(0.0);
    CHECK(h.cameras.fixed(fixed) != nullptr);
    CHECK(h.cameras.targets() == std::vector<double>{5.0});
    const double third = h.call("CameraCreateThird", {str("third"), Value(5.0), Value(60.0), Value(4.0), Value(1.0),
                                                      Value(0.0), triple(0, 0, 1.5), Value(0.1), Value(100.0)})
                             .number()
                             .value_or(0.0);
    CHECK(h.cameras.third(third) != nullptr);
    CHECK(h.call("CameraCreateThird", {str("third"), Value(0.0)}).number() == 0.0);
}
