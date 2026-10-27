// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc of what the mesh views draw (docs/guides/debug-menu.md#mesh-views): that with
// every view off the game draws as it does without them (the same passes, no call to the hook and, on a real window
// where one can be had, the same frame pixel for pixel), and the flat view's scope. The Null renderer the tests run on
// draws no pixels, so what CI sees of "no pixel changes" is the draws' bookkeeping; the pixel check skips without an
// OpenGL window. The hook's and the inspector's side is in disc_mesh_views_test.cpp. They run only when the
// environment variable CONEY_DISC names the disc and skip otherwise; they print counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "debug/entity_inspect.h"
#include "debug/mesh_views.h"
#include "graphics/texture_grid.h"
#include "platform/mesh_views.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "support/disc_mesh_views_fixtures.h"

namespace {

using coney::debug::EntityId;
using coney::test::mesh_views::humanIds;
using coney::test::mesh_views::MeshFacts;
using coney::test::mesh_views::meshFactsOf;
using coney::test::mesh_views::Opened;
using coney::test::mesh_views::openYard;
using coney::test::mesh_views::passesOfHumans;
using coney::test::mesh_views::RecordingHook;
using coney::test::mesh_views::windowAvailable;
using coney::test::mesh_views::Yard;

} // namespace

TEST_CASE("with every mesh view off the game draws as without the hook: the same passes and no call to it",
          "[disc][mesh][gl]") {
    // Expected: unverified, #210 (faithful first: the views change nothing while off)
    constexpr int kFrames = 20;
    enum class Hook : std::uint8_t { None, ViewsOff, RecordingOff, ViewsOn };
    // What one run of the yard came to: the passes every human was drawn in each frame, how often the hook was asked
    // and handed a model, and the flat passes the views drew.
    struct Run {
        Opened opened = Opened::NoDisc;
        std::vector<std::vector<std::size_t>> passes;
        int asked = 0;
        std::size_t handed = 0;
        std::size_t flatDraws = 0;
    };
    // The world is the same each run, so the passes the game draws are too, whatever the hook does.
    const auto play = [](Hook which) {
        Run result;
        Yard yard;
        result.opened = windowAvailable() ? openYard(yard, true) : Opened::NoWindow;
        if (result.opened != Opened::Window) {
            return result;
        }
        coney::debug::DebugDrawOptions options;
        if (which == Hook::ViewsOn) {
            options.mesh.view = coney::debug::MeshShading::Material;
            options.mesh.viewScope = coney::debug::MeshScope::Humans;
        }
        std::shared_ptr<coney::platform::MeshViews> views;
        std::shared_ptr<RecordingHook> recording;
        if (which == Hook::ViewsOff || which == Hook::ViewsOn) {
            views = std::make_shared<coney::platform::MeshViews>(options);
            yard.mode->setMeshViews(views);
        } else if (which == Hook::RecordingOff) {
            recording = std::make_shared<RecordingHook>();
            recording->on = false;
            yard.mode->setMeshViews(recording);
        }
        const coney::platform::PlayLevelControls controls(yard.mode.get());
        for (int i = 0; i < kFrames; ++i) {
            yard.run(1);
            result.passes.push_back(passesOfHumans(controls));
            if (views) {
                result.flatDraws += views->passDraws();
            }
            if (recording) {
                result.asked = recording->begun;
                result.handed += recording->calls.size();
            }
        }
        return result;
    };
    const Run plain = play(Hook::None);
    if (plain.opened == Opened::NoWindow) {
        SKIP("no OpenGL window: the Null renderer draws no model");
    }
    if (plain.opened == Opened::NoDisc) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    REQUIRE(plain.passes.size() == static_cast<std::size_t>(kFrames));
    // The humans are drawn at all: someone is in one pass or two.
    std::size_t drawnPasses = 0;
    for (const std::size_t passes : plain.passes.back()) {
        drawnPasses += passes;
    }
    CHECK(drawnPasses >= 3);

    // Off, through the real views or a hook that says no: the same passes, nothing drawn by the hook.
    const Run off = play(Hook::ViewsOff);
    CHECK(off.passes == plain.passes);
    CHECK(off.flatDraws == 0);
    const Run recordingOff = play(Hook::RecordingOff);
    CHECK(recordingOff.passes == plain.passes);
    CHECK(recordingOff.asked >= kFrames);
    CHECK(recordingOff.handed == 0);

    // On, the passes the game draws are still the same (the hook only adds its own), and the views draw theirs.
    const Run on = play(Hook::ViewsOn);
    CHECK(on.passes == plain.passes);
    CHECK(on.flatDraws > 0);
    std::printf("views off: %d frames, the humans' passes equal with no hook, a hook that is off and a flat view on "
                "(%zu flat draws)\n",
                kFrames, on.flatDraws);
}

TEST_CASE("the flat view draws the selection alone, or every human, and the models in scope", "[disc][mesh][gl]") {
    // Expected: unverified, #210 (docs/guides/debug-menu.md#mesh-views: the scope of a view)
    if (!windowAvailable()) {
        SKIP("no OpenGL window: the Null renderer draws no model");
    }
    Yard yard;
    if (openYard(yard, true) == Opened::NoDisc) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::platform::PlayLevelMode& mode = *yard.mode;
    const coney::platform::PlayLevelControls controls(&mode);
    const std::vector<EntityId> humans = humanIds(*controls.inspect());
    REQUIRE(humans.size() >= 3);
    coney::debug::DebugDrawOptions options;
    options.mesh.view = coney::debug::MeshShading::Material;
    options.mesh.viewScope = coney::debug::MeshScope::Selection;
    options.inspected = humans[1];
    const auto views = std::make_shared<coney::platform::MeshViews>(options);
    mode.setMeshViews(views);

    // The selection alone: a flat pass over each of its materials, none over anyone else.
    const std::optional<MeshFacts> selected = meshFactsOf(*controls.inspect(), humans[1]);
    REQUIRE(selected.has_value());
    yard.run(1);
    CHECK(views->passDraws() == selected->materials);

    // Every human: each one drawn as a body gets its materials' passes, the blood layer none.
    options.mesh.viewScope = coney::debug::MeshScope::Humans;
    yard.run(1);
    std::size_t expected = 0;
    for (const EntityId id : humans) {
        const std::optional<MeshFacts> facts = meshFactsOf(*controls.inspect(), id);
        REQUIRE(facts.has_value());
        expected += facts->passes > 0 ? facts->materials : 0;
    }
    CHECK(views->passDraws() == expected);
    CHECK(expected > selected->materials);

    // Everything: the sandbox's own blocks are drawn by its renderer, so the humans are all there is to draw here.
    options.mesh.viewScope = coney::debug::MeshScope::Everything;
    yard.run(1);
    CHECK(views->passDraws() >= expected);

    // No selection and the selection's scope: nothing is drawn flat, and the models are drawn as the game does.
    options.mesh.viewScope = coney::debug::MeshScope::Selection;
    options.inspected = coney::debug::kNoEntity;
    yard.run(1);
    CHECK(views->passDraws() == 0);
    std::printf("flat view: %zu passes on one human, %zu on every human\n", selected->materials, expected);
}

TEST_CASE("with every mesh view off a real frame is the same pixel for pixel as without the hook, and after "
          "--mesh-view's frames too",
          "[disc][mesh][gl]") {
    // Expected: unverified, #210 (faithful first: the views change no pixel while off)
    constexpr int kFrames = 24;
    constexpr int kOnFrames = 6;
    // The hash of every frame of a run of the yard on a hidden OpenGL window: with `views` as the hook (none: no hook);
    // the flat view over everything for the first `onFor` frames when positive. Empty when there is no disc or window.
    enum class Skip : std::uint8_t { None, NoDisc, NoWindow };
    Skip skipped = Skip::None;
    const auto frames = [&skipped](std::shared_ptr<coney::platform::MeshViews> views,
                                   coney::debug::DebugDrawOptions& options, int onFor) {
        std::vector<std::uint64_t> hashes;
        Yard yard;
        const Opened opened = windowAvailable() ? openYard(yard, true) : Opened::NoWindow;
        if (opened != Opened::Window) {
            skipped = opened == Opened::NoDisc ? Skip::NoDisc : Skip::NoWindow;
            return hashes;
        }
        yard.mode->setMeshViews(std::move(views));
        yard.engine->setFrameTap([&hashes](std::span<const std::uint8_t> pixels, coney::graphics::Extent) {
            // FNV-1a over the frame's bytes.
            std::uint64_t hash = 0xcbf29ce484222325ULL;
            for (const std::uint8_t byte : pixels) {
                hash = (hash ^ byte) * 0x100000001b3ULL;
            }
            hashes.push_back(hash);
        });
        for (int i = 0; i < kFrames; ++i) {
            if (i == 0 && onFor > 0) {
                options.mesh.view = coney::debug::MeshShading::Material;
                options.mesh.viewScope = coney::debug::MeshScope::Everything;
            }
            if (i == onFor) {
                options.mesh = coney::debug::MeshViewOptions{};
            }
            yard.run(1);
        }
        yard.engine->setFrameTap({});
        return hashes;
    };
    coney::debug::DebugDrawOptions none;
    const std::vector<std::uint64_t> plain = frames(nullptr, none, 0);
    if (skipped == Skip::NoDisc) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    if (skipped == Skip::NoWindow) {
        SKIP("no OpenGL window: the pixels cannot be compared here");
    }
    REQUIRE(plain.size() >= static_cast<std::size_t>(kFrames));

    coney::debug::DebugDrawOptions off;
    const std::vector<std::uint64_t> withViews = frames(std::make_shared<coney::platform::MeshViews>(off), off, 0);
    CHECK(withViews == plain);

    // Switched on for the first frames, the frames differ (the hook reaches what is drawn); once off again they are
    // the plain run's, which `--mesh-view ...,frames=N` relies on.
    coney::debug::DebugDrawOptions on;
    const std::vector<std::uint64_t> flat = frames(std::make_shared<coney::platform::MeshViews>(on), on, kOnFrames);
    REQUIRE(flat.size() == plain.size());
    std::size_t differing = 0;
    for (int i = 0; i < kOnFrames; ++i) {
        differing += flat[static_cast<std::size_t>(i)] != plain[static_cast<std::size_t>(i)] ? 1 : 0;
    }
    CHECK(differing > 0);
    for (std::size_t i = static_cast<std::size_t>(kOnFrames); i < plain.size(); ++i) {
        CHECK(flat[i] == plain[i]);
    }
    std::printf("frame identity: %zu frames equal with the views off; %zu of the first %d differ with a flat view on\n",
                plain.size(), differing, kOnFrames);
}
